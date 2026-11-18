# Videos

Both videos are recorded from the real app, nothing is mocked up.

| File | Length | What it is |
|---|---|---|
| [walkthrough.mp4](walkthrough.mp4) | 4:57, 1920 x 1080 | The full walkthrough. I explain in plain words what the engine does, how the two methods work, and what each scene shows. This is the one to share on a portfolio or LinkedIn |
| [demo_short.mp4](demo_short.mp4) | 0:56, 1920 x 1080 | A quick tour with short labels |
| [demo_short.gif](demo_short.gif) | 0:56, 800 px wide | The same short tour as a GIF (it's the one at the top of the README) |

Neither video has sound, so they work fine when a feed autoplays them muted.

## Rebuilding them

`src/make_videos.py` writes an app script, runs the app twice (once to find where the buttons are, once to capture frames), then adds the cursor and the text and encodes with ffmpeg. It needs a release build, Python with Pillow, and ffmpeg on `PATH`.

```powershell
python docs\media\src\make_videos.py walkthrough
python docs\media\src\make_videos.py short
```

The text of the walkthrough lives in `BEATS` in `make_videos.py`.
