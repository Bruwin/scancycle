import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { fileURLToPath } from "node:url";
import net from "node:net";
import path from "node:path";
import { chromium } from "playwright";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const idePath = path.resolve(root, process.argv[2] ?? "build/bin/scancycle_ide");

const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));

function log(message) {
    console.log(`[free-play-smoke] ${message}`);
}

function freePort() {
    return new Promise((resolve, reject) => {
        const server = net.createServer();
        server.on("error", reject);
        server.listen(0, "127.0.0.1", () => {
            const address = server.address();
            server.close(() => resolve(address.port));
        });
    });
}

async function waitForServer(url, child) {
    const deadline = Date.now() + 10000;
    while (Date.now() < deadline) {
        if (child.exitCode !== null) {
            throw new Error(`free play exited with status ${child.exitCode}`);
        }
        try {
            const response = await fetch(`${url}/`, { signal: AbortSignal.timeout(500) });
            if (response.ok) {
                return;
            }
        } catch {
            // The listener is still starting.
        }
        await sleep(100);
    }
    throw new Error(`free play did not become ready at ${url}`);
}

async function startFreePlay() {
    const port = await freePort();
    const child = spawn(idePath, ["--free-play", `--port=${port}`], {
        cwd: root,
        stdio: ["ignore", "pipe", "pipe"],
    });
    let output = "";
    child.stdout.on("data", (chunk) => {
        output += chunk;
    });
    child.stderr.on("data", (chunk) => {
        output += chunk;
    });
    const url = `http://127.0.0.1:${port}`;
    try {
        await waitForServer(url, child);
    } catch (error) {
        child.kill("SIGKILL");
        throw new Error(`${error.message}\n${output}`);
    }
    return { child, url, output: () => output };
}

async function stopServer(server) {
    if (!server || server.child.exitCode !== null) {
        return;
    }
    server.child.kill("SIGTERM");
    await Promise.race([once(server.child, "exit"), sleep(2000)]);
    if (server.child.exitCode === null) {
        server.child.kill("SIGKILL");
    }
}

async function postAndWait(page, action, buttonName) {
    const response = page.waitForResponse(
        (candidate) =>
            candidate.request().method() === "POST" &&
            new URL(candidate.url()).pathname === action,
        { timeout: 5000 },
    );
    await page.getByRole("button", { name: buttonName }).click();
    assert.equal((await response).status(), 303, `${buttonName} redirects`);
    await page.waitForLoadState("load");
}

async function main() {
    let server;
    let browser;
    let context;
    try {
        server = await startFreePlay();
        browser = await chromium.launch({ headless: true, args: ["--no-sandbox"] });
        context = await browser.newContext({ viewport: { width: 1280, height: 900 } });
        const page = await context.newPage();
        page.setDefaultTimeout(5000);

        const browserErrors = [];
        page.on("console", (message) => {
            if (message.type() === "error" && !message.text().includes("favicon.ico")) {
                browserErrors.push(message.text());
            }
        });
        page.on("pageerror", (error) => browserErrors.push(String(error)));

        await page.goto(server.url, { waitUntil: "load" });
        await page.locator("[data-free-play]").waitFor({ state: "visible" });
        assert.match(await page.locator("[data-free-play]").innerText(), /Free Play/i);
        assert.match(await page.locator("body").innerText(), /no grades/i);
        assert.match(await page.locator("body").innerText(), /not a security sandbox/i);
        assert.equal(
            await page.locator('form[action="/grade"], a[href*="/grade"]').count(),
            0,
            "Free Play has no grade UI",
        );

        const scanLine = page.locator("p").filter({ hasText: "Scan" }).first();
        assert.match(await scanLine.innerText(), /Scan\s+0/);
        await postAndWait(page, "/step", "Step one scan");
        assert.match(await scanLine.innerText(), /Scan\s+1/);

        const source = page.locator('textarea[name="source"]');
        const starter = await source.inputValue();
        await source.fill("PROGRAM Broken\nVAR\n    x : BOOL;\n");
        await postAndWait(page, "/apply", "Compile & load");
        assert.match(await page.locator("body").innerText(), /not compiled/i);

        await postAndWait(page, "/reset", "Reset to starter");
        assert.equal(await source.inputValue(), starter, "Reset restores the starter");
        assert.match(await scanLine.innerText(), /Scan\s+0/);
        assert.ok((await page.locator("body").innerText()).includes("ready"));

        assert.deepEqual(browserErrors, [], `browser errors: ${browserErrors.join("; ")}`);
        log("load, compile, step, reset and no-grade UI passed");
    } catch (error) {
        console.error("[free-play-smoke] failure");
        if (server) {
            console.error(server.output());
        }
        throw error;
    } finally {
        await context?.close();
        await browser?.close();
        await stopServer(server);
    }
}

await main();
