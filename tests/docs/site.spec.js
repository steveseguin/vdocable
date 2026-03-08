const { test, expect } = require("@playwright/test");

const latestBase = "https://github.com/steveseguin/vdocable/releases/latest/download";

test("landing page exposes stable release links", async ({ page }) => {
  await page.goto("/");

  await expect(page.getByRole("heading", { name: "VDO Cable" })).toBeVisible();
  await expect(page.getByRole("img", { name: /VDO Cable app showing multiple application routes/i })).toBeVisible();
  await expect(page.getByRole("link", { name: "Download Installer" })).toHaveAttribute(
    "href",
    `${latestBase}/vdocable-setup.exe`
  );
  await expect(page.getByRole("link", { name: "Portable EXE" })).toHaveAttribute(
    "href",
    `${latestBase}/vdocable-portable.exe`
  );
  await expect(page.getByRole("link", { name: "ZIP Package" })).toHaveAttribute(
    "href",
    `${latestBase}/vdocable-win64.zip`
  );
  await expect(page.getByRole("link", { name: "Getting Started" })).toHaveAttribute(
    "href",
    "./getting-started.html"
  );
  await expect(page.getByText("Duplicate ID guardrails")).toBeVisible();
  await expect(page.getByText("Conservative signaling")).toBeVisible();
});

test("getting started page covers install and routing flow", async ({ page }) => {
  await page.goto("/getting-started.html");

  await expect(page.getByRole("heading", { name: "Getting Started" })).toBeVisible();
  await expect(page.getByText("Install the signed Windows build")).toBeVisible();
  await expect(page.getByText("Add one route per application")).toBeVisible();
  await expect(page.getByText("blocks the second publish before it hits the server")).toBeVisible();
  await expect(page.getByRole("link", { name: "Latest installer" })).toHaveAttribute(
    "href",
    `${latestBase}/vdocable-setup.exe`
  );
});
