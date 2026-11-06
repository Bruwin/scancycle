import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { fileURLToPath } from "node:url";
import net from "node:net";
import path from "node:path";
import { chromium } from "playwright";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const idePath = path.resolve(root, process.argv[2] ?? "build/bin/scancycle_ide");
const ladderProgram = path.join(root, "examples/batch_count.ld");
const sfcProgram = path.join(root, "examples/press_cycle.sfc");

const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));

function log(message) {
    console.log(`[browser-smoke] ${message}`);
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
            throw new Error(`editor exited with status ${child.exitCode}`);
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
    throw new Error(`editor did not become ready at ${url}`);
}

async function startEditor(program) {
    const port = await freePort();
    const child = spawn(idePath, [`--port=${port}`, `--program=${program}`], {
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

async function stopEditor(server) {
    if (!server || server.child.exitCode !== null) {
        return;
    }
    server.child.kill("SIGTERM");
    await Promise.race([once(server.child, "exit"), sleep(2000)]);
    if (server.child.exitCode === null) {
        server.child.kill("SIGKILL");
    }
}

async function waitForEditor(page) {
    await page.waitForSelector("[data-zoom-controls]:not([hidden])", { timeout: 5000 });
}

async function canvasSmoke(page, label) {
    const canvas = page.locator("[data-zoomable]").first();
    await canvas.waitFor({ state: "visible" });
    const viewport = canvas.locator("[data-zoom-viewport]");
    const svg = canvas.locator("svg").first();
    const level = canvas.locator("[data-zoom-level]");
    const zoomIn = canvas.locator("[data-zoom-in]");
    const reset = canvas.locator("[data-zoom-reset]");

    const initialWidth = await svg.evaluate((node) => node.getBoundingClientRect().width);
    assert.equal((await level.textContent()).trim(), "100%", `${label} starts at 100%`);

    await zoomIn.click();
    await page.waitForTimeout(80);
    assert.equal((await level.textContent()).trim(), "125%", `${label} zooms to 125%`);
    const zoomedWidth = await svg.evaluate((node) => node.getBoundingClientRect().width);
    assert.ok(zoomedWidth > initialWidth, `${label} grows when zoomed`);

    await reset.click();
    await page.waitForTimeout(80);
    assert.equal((await level.textContent()).trim(), "100%", `${label} resets to 100%`);
    const resetWidth = await svg.evaluate((node) => node.getBoundingClientRect().width);
    assert.ok(Math.abs(resetWidth - initialWidth) < 2, `${label} restores the fitted width`);

    for (let i = 0; i < 10; i++) {
        if (await zoomIn.isDisabled()) {
            break;
        }
        await zoomIn.click();
    }
    assert.equal((await level.textContent()).trim(), "300%", `${label} clamps at 300%`);

    const pan = await page.evaluate(() => {
        const node = document.querySelector("[data-zoomable] [data-zoom-viewport]");
        node.scrollLeft = 120;
        node.scrollTop = 40;
        const rect = node.getBoundingClientRect();
        const starts = [
            Math.floor(rect.left + rect.width * 0.6),
            Math.floor(rect.left + 12),
        ];
        for (const start of starts) {
            for (let y = rect.top + 12; y < rect.bottom - 12; y += 12) {
                for (let x = start; x < rect.right - 12; x += 12) {
                    const element = document.elementFromPoint(x, y);
                    if (element && !element.closest("a,button")) {
                        return {
                            x,
                            y,
                            left: Math.round(node.scrollLeft),
                            top: Math.round(node.scrollTop),
                        };
                    }
                }
            }
        }
        return null;
    });
    assert.ok(pan, `${label} has a draggable area`);

    await page.mouse.move(pan.x, pan.y);
    await page.mouse.down();
    await page.mouse.move(pan.x - 80, pan.y - 20, { steps: 8 });
    await page.mouse.up();
    await page.waitForTimeout(80);
    const afterPan = await viewport.evaluate((node) => ({
        left: Math.round(node.scrollLeft),
        top: Math.round(node.scrollTop),
    }));
    assert.ok(afterPan.left > pan.left + 10, `${label} drags horizontally`);

    await reset.click();
    await page.waitForTimeout(80);
    assert.equal((await level.textContent()).trim(), "100%", `${label} resets after panning`);
    log(`${label}: zoom, reset and drag pan passed`);
}

async function keyboardSmoke(page, ladderUrl) {
    await page.goto(ladderUrl, { waitUntil: "load" });
    await waitForEditor(page);

    const contact = page.locator('a[aria-label^="Toggle contact"]').first();
    assert.equal(await contact.count(), 1, "ladder has a focusable contact");
    assert.equal(await contact.getAttribute("tabindex"), "0", "contact is in the tab order");
    const label = await contact.getAttribute("aria-label");

    await contact.focus();
    const focusedLabel = await page.evaluate(() =>
        document.activeElement?.getAttribute("aria-label"),
    );
    assert.equal(focusedLabel, label, "contact receives keyboard focus");
    const outline = await contact.evaluate((node) => getComputedStyle(node).outlineStyle);
    assert.notEqual(outline, "none", "focused contact has a visible outline");

    const before = await page.locator("pre").first().textContent();
    const [response] = await Promise.all([
        page.waitForResponse(
            (candidate) =>
                candidate.request().method() === "GET" &&
                new URL(candidate.url()).pathname === "/toggle",
            { timeout: 5000 },
        ),
        page.keyboard.press("Enter"),
    ]);
    assert.equal(response.status(), 303, "Enter follows the toggle route");
    await page.waitForLoadState("load");
    await page.waitForFunction(
        (previous) => document.querySelector("pre")?.textContent !== previous,
        before,
        { timeout: 5000 },
    );
    await waitForEditor(page);

    const after = await page.locator("pre").first().textContent();
    assert.notEqual(after, before, "Enter changes the ladder source");
    log(`ladder: keyboard toggle ${label} passed`);
}

async function main() {
    let ladder;
    let sfc;
    let browser;
    let context;
    try {
        ladder = await startEditor(ladderProgram);
        sfc = await startEditor(sfcProgram);
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

        await keyboardSmoke(page, ladder.url);
        await page.goto(ladder.url, { waitUntil: "load" });
        await waitForEditor(page);
        await canvasSmoke(page, "ladder");

        await page.goto(`${ladder.url}/view?mode=fbd`, { waitUntil: "load" });
        await waitForEditor(page);
        assert.ok((await page.locator("svg.fbd").count()) > 0, "FBD view renders");
        const fbdLabel = await page
            .locator("[data-zoom-viewport]")
            .first()
            .getAttribute("aria-label");
        assert.match(fbdLabel, /^Network 1 diagram\./, "FBD canvas is labelled by network");
        await canvasSmoke(page, "FBD");

        await page.goto(sfc.url, { waitUntil: "load" });
        await waitForEditor(page);
        assert.equal(await page.locator("svg.sfc").count(), 1, "SFC view renders");
        await canvasSmoke(page, "SFC");

        assert.deepEqual(browserErrors, [], `browser errors: ${browserErrors.join("; ")}`);
        log("passed");
    } catch (error) {
        console.error("[browser-smoke] failure");
        if (ladder) {
            console.error(ladder.output());
        }
        if (sfc) {
            console.error(sfc.output());
        }
        throw error;
    } finally {
        await context?.close();
        await browser?.close();
        await stopEditor(ladder);
        await stopEditor(sfc);
    }
}

await main();
