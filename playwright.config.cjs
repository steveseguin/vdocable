const { defineConfig } = require("@playwright/test");

module.exports = defineConfig({
  testDir: "./tests/docs",
  timeout: 30000,
  retries: process.env.CI ? 1 : 0,
  use: {
    baseURL: "http://127.0.0.1:4173",
    headless: true
  },
  projects: [
    {
      name: "chromium",
      use: {
        browserName: "chromium",
        launchOptions: {
          args: ["--autoplay-policy=no-user-gesture-required"]
        }
      }
    }
  ],
  webServer: {
    command: "npx http-server docs -p 4173 -c-1",
    port: 4173,
    reuseExistingServer: !process.env.CI,
    timeout: 30000
  }
});
