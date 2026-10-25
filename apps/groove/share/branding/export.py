"""Export the reel's original mascot and settled wordmark on transparency.

Uses the same Playwright/Chromium setup as capture.py; no video render needed.
"""

import base64
import os
from pathlib import Path

from playwright.sync_api import sync_playwright

motion = Path(__file__).resolve().parents[2] / "reel" / "motion"
output = Path(__file__).with_name("groove.png")

with sync_playwright() as p:
    browser = p.chromium.launch(
        executable_path=os.environ.get("CHROMIUM_EXECUTABLE"),
        args=["--allow-file-access-from-files", "--disable-web-security"],
    )
    page = browser.new_page()
    page.goto((motion / "index.html").as_uri())
    page.evaluate("window.ready")
    encoded = page.evaluate("""() => {
      const brand = mk(1536,512), c = brand.getContext('2d');
      c.drawImage(IMG.hero,0,0,512,512);
      const background = mk(1536,512), foreground = mk(1536,512);
      const back = background.getContext('2d'), front = foreground.getContext('2d');
      const word = 'Groove', size = 250, tracking = size * .02;
      const colors = ['#ffd21e','#ff7a2a','#ff4fa0','#36c95c','#1aa6f2','#a047f2'];
      c.font = `700 ${size}px Fred`;
      const widths = [...word].map(ch => c.measureText(ch).width);
      let x = 1025 - (widths.reduce((sum,w) => sum + w,0) + tracking * (word.length-1)) / 2;
      [...word].forEach((ch,i) => {
        const center = x + widths[i] / 2;
        sticker(back,ch,center,330,size,colors[i],{depth:20});
        sticker(front,ch,center,330,size,colors[i],{depth:0,ink:'transparent'});
        x += widths[i] + tracking;
      });
      c.drawImage(background,0,0);
      c.drawImage(foreground,0,0);
      return brand.toDataURL('image/png').split(',')[1];
    }""")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(base64.b64decode(encoded))
    browser.close()
print(output)
