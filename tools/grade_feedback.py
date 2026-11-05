#!/usr/bin/env python3
"""Drafts the note a teacher sends back after a graded attempt.

The portal already records what a submission did: every rubric row, the detail
the simulation produced, and the program the student handed in. Turning that
into a sentence a student can act on is the part a human still does by hand, so
this tool asks a model for a first draft and leaves the teacher to edit it.

The model call goes through the Respan gateway (https://api.respan.ai/api), so
the only credential involved is RESPAN_API_KEY and no provider key is needed.
Without that key the tool still explains the attempt and prints the exact prompt
it would have sent, which is what makes it usable on a machine with no account.

    python3 tools/grade_feedback.py --records=grades.tsv
    python3 tools/grade_feedback.py --records=grades.tsv --student=Ada --dry-run
"""

from __future__ import annotations

import argparse
import os
import sys
from dataclasses import dataclass, field
from pathlib import Path
from urllib.parse import unquote

# The gateway is OpenAI-compatible, so one client reaches every model. The base
# URL is a constant rather than an env var: a project this small has one
# gateway, and an enterprise endpoint is the only thing worth overriding.
GATEWAY_BASE_URL = "https://api.respan.ai/api"
DEFAULT_MODEL = "gpt-5.5"

REPO_ROOT = Path(__file__).resolve().parent.parent


@dataclass
class Check:
    passed: bool
    description: str
    detail: str


@dataclass
class Attempt:
    at: int
    lesson_id: str
    passed: bool
    passed_count: int
    total: int
    program: str
    checks: list[Check] = field(default_factory=list)
    student: str = ""

    @property
    def score(self) -> str:
        return f"{self.passed_count}/{self.total}"

    @property
    def program_label(self) -> str:
        return "the student's program" if self.program else "the reference program"


def parse_checks(field_text: str) -> list[Check]:
    """Each row is `passed,description,detail`, both texts percent-encoded.

    A description holding a comma or a newline would otherwise split the entry
    apart, which is the same reason the log encodes every field.
    """
    checks: list[Check] = []
    if not field_text:
        return checks
    for entry in field_text.split(";"):
        parts = entry.split(",")
        if len(parts) != 3:
            continue
        checks.append(
            Check(
                passed=parts[0] == "1",
                description=unquote(parts[1]),
                detail=unquote(parts[2]),
            )
        )
    return checks


def parse_line(line: str) -> Attempt | None:
    """One log line, or None when the line is not a record this tool knows.

    A log has outlived two format changes already, so a short row is a record
    from before that column existed rather than a corrupt file.
    """
    fields = line.rstrip("\n").split("\t")
    if len(fields) < 6 or len(fields) > 8:
        return None
    try:
        at = int(fields[0])
        passed = fields[2] == "1"
        passed_count = int(fields[3])
        total = int(fields[4])
    except ValueError:
        return None
    return Attempt(
        at=at,
        lesson_id=unquote(fields[1]),
        passed=passed,
        passed_count=passed_count,
        total=total,
        program=unquote(fields[5]),
        checks=parse_checks(fields[6]) if len(fields) >= 7 else [],
        student=unquote(fields[7]) if len(fields) == 8 else "",
    )


def read_attempts(path: Path) -> list[Attempt]:
    attempts: list[Attempt] = []
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except FileNotFoundError:
        return attempts
    for line in lines:
        attempt = parse_line(line)
        if attempt is not None:
            attempts.append(attempt)
    return attempts


def build_prompt(attempt: Attempt) -> list[dict[str, str]]:
    rows = []
    for check in attempt.checks:
        verdict = "passed" if check.passed else "not met"
        rows.append(f"- {check.description} ({verdict}): {check.detail}")
    rubric = "\n".join(rows) if rows else "- (this attempt predates stored rubric rows)"
    program = attempt.program or "(empty: the reference program graded itself)"

    system = (
        "You help a mechatronics teacher write feedback on a graded PLC exercise "
        "in Scan Cycle. The student programs a factory cell in IEC 61131-3; the "
        "grader runs that program against a deterministic simulation and checks "
        "the rubric. Write three to five sentences addressed to the student: what "
        "their program actually did, why the unmet checks failed, and one concrete "
        "next step. Name the specific checks. Do not rewrite the program and do "
        "not invent behaviour the rubric did not observe."
    )
    user = (
        f"Lesson: {attempt.lesson_id}\n"
        f"Student: {attempt.student or 'unnamed'}\n"
        f"Result: {'passed' if attempt.passed else 'not passed'} "
        f"({attempt.score} checks)\n"
        f"Rubric:\n{rubric}\n\n"
        f"Submitted program ({attempt.program_label}):\n```\n{program}\n```\n"
    )
    return [
        {"role": "system", "content": system},
        {"role": "user", "content": user},
    ]


def respan_api_key() -> str:
    """RESPAN_API_KEY from the environment, else from an untracked .env.

    The environment wins so a harness or a shell export can override the file
    without editing it.
    """
    key = os.environ.get("RESPAN_API_KEY", "").strip()
    if key:
        return key
    env_file = REPO_ROOT / ".env"
    if not env_file.is_file():
        return ""
    for line in env_file.read_text(encoding="utf-8", errors="replace").splitlines():
        name, _, value = line.partition("=")
        if name.strip() == "RESPAN_API_KEY":
            return value.strip().strip("\"'")
    return ""


def request_feedback(attempt: Attempt, model: str, key: str) -> str:
    from openai import OpenAI

    client = OpenAI(api_key=key, base_url=GATEWAY_BASE_URL)
    response = client.chat.completions.create(
        model=model,
        messages=build_prompt(attempt),
        # Metadata is what makes this run findable in the gateway log next to
        # every other portal request.
        extra_body={"metadata": {"tool": "grade_feedback", "lesson": attempt.lesson_id}},
    )
    return response.choices[0].message.content or ""


def pick_attempt(attempts: list[Attempt], args: argparse.Namespace) -> Attempt | None:
    wanted = [
        attempt
        for attempt in attempts
        if (not args.lesson or attempt.lesson_id == args.lesson)
        and (not args.student or attempt.student == args.student)
    ]
    if not wanted:
        return None
    return wanted[-args.index] if args.index <= len(wanted) else None


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--records", default="grades.tsv", help="the portal's grade log")
    parser.add_argument("--lesson", default="", help="only attempts on this lesson id")
    parser.add_argument("--student", default="", help="only attempts by this student")
    parser.add_argument("--index", type=int, default=1, help="1 is the newest match")
    parser.add_argument("--model", default=DEFAULT_MODEL, help="a model the gateway offers")
    parser.add_argument("--dry-run", action="store_true", help="print the prompt, call nothing")
    args = parser.parse_args(argv)
    if args.index < 1:
        parser.error("--index counts back from the newest match, so it starts at 1")

    attempts = read_attempts(Path(args.records))
    attempt = pick_attempt(attempts, args)
    if attempt is None:
        print(
            f"grade_feedback: no matching attempt in '{args.records}'. "
            "Grade a lesson over the portal first.",
            file=sys.stderr,
        )
        return 1

    key = respan_api_key()
    if args.dry_run or not key:
        if not args.dry_run:
            print("grade_feedback: RESPAN_API_KEY is not set; printing the prompt instead.")
        for message in build_prompt(attempt):
            print(f"=== {message['role']} ===")
            print(message["content"])
        return 0

    try:
        print(request_feedback(attempt, args.model, key))
    except ImportError:
        print(
            "grade_feedback: the openai package is missing. Install it with\n"
            "    python3 -m pip install -r tools/requirements.txt",
            file=sys.stderr,
        )
        return 1
    except Exception as error:  # noqa: BLE001 - the CLI reports what the gateway said
        print(f"grade_feedback: the gateway request failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
