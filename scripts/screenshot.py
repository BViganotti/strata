#!/usr/bin/env python3
"""Capture a deterministic screenshot of a running strata server.

Usage:
    python3 scripts/screenshot.py http://127.0.0.1:7777/#<sha> docs/demo.png

Requires Playwright with Chromium installed:
    pip install playwright && playwright install chromium
"""
import sys

from playwright.sync_api import sync_playwright

url = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:7777/"
out = sys.argv[2] if len(sys.argv) > 2 else "docs/demo.png"

with sync_playwright() as p:
    browser = p.chromium.launch()
    page = browser.new_page(viewport={"width": 1600, "height": 1000}, device_scale_factor=1)
    page.goto(url)
    # Let the force layout settle and the inspector transition finish.
    page.wait_for_timeout(2500)
    page.screenshot(path=out)
    browser.close()
