"""Builds the two demo videos from real app frames.

    python make_videos.py walkthrough   ->  docs/media/walkthrough.mp4  (typed first-person text column)
    python make_videos.py short         ->  docs/media/demo_short.mp4 + demo_short.gif (small blurbs)

How it works
  1. A generator writes an app script (mouse moves, clicks, keys, captures) for the whole video.
  2. Pass 1 runs that script without captures and prints where panel buttons are and how the room
     maps to the screen at each point. Pass 2 regenerates the script with those positions, so the
     cursor can travel like a hand would (curved paths, overshoot, small corrections) and still land
     exactly on each button.
  3. Pass 2 captures one frame per video frame that changes; holds repeat a frame.
  4. Compose draws the cursor (the hidden capture window has none), the text and the fades, and
     pipes frames to ffmpeg.
"""
import json, math, os, random, shutil, subprocess, sys, time
from PIL import Image, ImageDraw, ImageFont, ImageFilter

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
APP = os.path.join(REPO, "build", "release", "app", "vis_app.exe")
OUT = os.path.join(REPO, "docs", "media")
WORK_ROOT = os.environ.get("VIDEO_WORK", os.path.join(os.environ.get("TEMP", "."), "vis_video_work"))
FPS = 30
FONT_REG = r"C:\Windows\Fonts\segoeui.ttf"
FONT_SB = r"C:\Windows\Fonts\seguisb.ttf"
FONT_BOLD = r"C:\Windows\Fonts\segoeuib.ttf"

# Light positions after each scene loads (size 1000, seed 1), measured with `print light_x light_y`.
LIGHTS = {"demo": (610.0, 290.0), "forest": (870.0, 430.0), "dungeon": (860.0, 280.0),
          "crisscross": (590.0, 710.0), "storeroom": (938.969, 504.324)}
SCENE_KEY = {"forest": "1", "dungeon": "2", "crisscross": "3", "storeroom": "4"}
SCENE_LABEL = {"forest": "Forest", "dungeon": "Dungeon", "crisscross": "Crisscross", "storeroom": "Storeroom",
               "demo": "Demo"}


# =================================================================================================
# motion
# =================================================================================================
def minjerk(t):
    t = max(0.0, min(1.0, t))
    return t * t * t * (10 - 15 * t + 6 * t * t)


def human_path(a, b, rng, speed=1.0):
    """Screen points (one per video frame) for a hand moving the mouse from a to b: a slightly bent
    path, a smooth start and stop, a small overshoot and correction on long moves, a little tremor."""
    d = math.dist(a, b)
    if d < 1.5:
        return [b]
    T = (0.30 + 0.12 * math.log2(1 + d / 30.0)) * rng.uniform(0.9, 1.15) / speed
    n = max(5, round(T * FPS))
    ux, uy = (b[0] - a[0]) / d, (b[1] - a[1]) / d
    px, py = -uy, ux
    bend = rng.uniform(-0.14, 0.14) * d
    overshoot = d > 160
    end = (b[0] + ux * d * rng.uniform(0.015, 0.04) + px * rng.uniform(-4, 4),
           b[1] + uy * d * rng.uniform(0.015, 0.04) + py * rng.uniform(-4, 4)) if overshoot else b
    c = ((a[0] + end[0]) / 2 + px * bend, (a[1] + end[1]) / 2 + py * bend)
    main_n = round(n * 0.8) if overshoot else n
    ph1, ph2 = rng.uniform(0, 6.28), rng.uniform(0, 6.28)
    pts = []
    for i in range(1, main_n + 1):
        s = minjerk(i / main_n)
        x = (1 - s) ** 2 * a[0] + 2 * (1 - s) * s * c[0] + s * s * end[0]
        y = (1 - s) ** 2 * a[1] + 2 * (1 - s) * s * c[1] + s * s * end[1]
        w = math.sin(math.pi * i / main_n)  # tremor fades at both ends
        pts.append((x + 0.7 * w * math.sin(i * 0.9 + ph1), y + 0.7 * w * math.sin(i * 1.3 + ph2)))
    if overshoot:
        m = max(4, n - main_n)
        for i in range(1, m + 1):
            s = minjerk(i / m)
            pts.append((end[0] + (b[0] - end[0]) * s, end[1] + (b[1] - end[1]) * s))
    pts[-1] = b
    return pts


def catmull_dense(pts, per=30):
    p = [pts[0]] + list(pts) + [pts[-1]]
    out = []
    for i in range(1, len(p) - 2):
        a, b, c, d = p[i - 1], p[i], p[i + 1], p[i + 2]
        for k in range(per):
            t = k / per
            out.append(tuple(0.5 * (2 * b[j] + (-a[j] + c[j]) * t + (2 * a[j] - 5 * b[j] + 4 * c[j] - d[j]) * t * t
                                    + (-a[j] + 3 * b[j] - 3 * c[j] + d[j]) * t ** 3) for j in (0, 1)))
    out.append(pts[-1])
    return out


def wander(points, frames, rng):
    """World points for a hand dragging something through `points` in about `frames` frames:
    each leg eases in and out, legs get uneven speeds, and there are short pauses now and then."""
    legs = list(zip(points[:-1], points[1:]))
    lens = [max(1.0, math.dist(a, b)) ** 0.8 for a, b in legs]
    pauses = [rng.choice([0, 0, 2, 4, 6]) for _ in legs]
    usable = max(len(legs) * 4, frames - sum(pauses))
    out = []
    for (a, b), L, pz in zip(legs, lens, pauses):
        n = max(4, round(usable * L / sum(lens)))
        dense = catmull_dense([a, ((a[0] + b[0]) / 2 + rng.uniform(-25, 25), (a[1] + b[1]) / 2 + rng.uniform(-25, 25)), b])
        cum = [0.0]
        for i in range(1, len(dense)):
            cum.append(cum[-1] + math.dist(dense[i], dense[i - 1]))
        j = 0
        for i in range(1, n + 1):
            target = cum[-1] * minjerk(i / n)
            while j < len(cum) - 2 and cum[j + 1] < target:
                j += 1
            seg = (cum[j + 1] - cum[j]) or 1.0
            f = (target - cum[j]) / seg
            out.append((dense[j][0] + f * (dense[j + 1][0] - dense[j][0]),
                        dense[j][1] + f * (dense[j + 1][1] - dense[j][1])))
        out.extend([out[-1]] * pz)
    return out


# =================================================================================================
# script generator
# =================================================================================================
class Gen:
    def __init__(self, work, known, capture):
        self.work = work.replace(os.sep, "/")
        self.known = known              # probe index -> printed values (pass 2)
        self.capture = capture          # pass 2
        self.L = ["# generated by make_videos.py"]
        self.frames = []                # dict(rep, down)
        self.vf = 0                     # video frames so far
        self.nprobe = 0
        self.mouse = (300.0, 300.0)
        self.down = None
        self.rng = random.Random(11)
        self.light = LIGHTS["demo"]
        self.marks = {}                 # name -> video frame
        self.cmd(f"move {self.mouse[0]} {self.mouse[1]}")

    def cmd(self, s):
        self.L.append(s)

    def mark(self, name):
        self.marks[name] = self.vf

    # ---- frames ----
    def frame(self, rep=1):
        if self.capture:
            self.cmd(f"capture {self.work}/raw/f{len(self.frames):05d}.bmp")
            self.cmd("print mouse_x mouse_y cursor")
        else:
            self.cmd("wait 1")
        self.frames.append(dict(rep=rep, down=self.down))
        self.vf += rep

    def hold(self, sec):
        self.cmd("wait 3")
        self.frame(rep=max(1, round(sec * FPS)))

    def hold_until(self, vf):
        if vf > self.vf:
            self.cmd("wait 3")
            self.frame(rep=vf - self.vf)

    # ---- probes (pass 1 prints, pass 2 reads) ----
    def probe(self, fields, pre=None):
        k = self.nprobe
        self.nprobe += 1
        if self.capture:
            return self.known[str(k)]
        if pre:
            self.cmd(pre)
        self.cmd("print " + " ".join(fields))
        return None

    def view(self):
        v = self.probe(["scale", "origin_x", "origin_y"])
        return None if v is None else (float(v["scale"]), float(v["origin_x"]), float(v["origin_y"]))

    def widget(self, label):
        v = self.probe(["mouse_x", "mouse_y"], pre=f"move @{label}")
        return None if v is None else (float(v["mouse_x"]), float(v["mouse_y"]))

    # ---- mouse ----
    def move(self, p):
        self.mouse = p
        self.cmd(f"move {p[0]:.2f} {p[1]:.2f}")

    def glide(self, p, speed=1.0):
        for q in human_path(self.mouse, p, self.rng, speed):
            self.move(q)
            self.frame()

    def world_move(self, w, view):
        if view is None:     # pass 1: let the app map it
            self.cmd(f"wmove {w[0]:.3f} {w[1]:.3f}")
        else:
            self.move((view[1] + view[0] * w[0], view[2] + view[0] * w[1]))

    def glide_world(self, w, view, speed=1.0):
        if view is None:
            self.cmd(f"wmove {w[0]:.3f} {w[1]:.3f}")
            self.frame()
        else:
            self.glide((view[1] + view[0] * w[0], view[2] + view[0] * w[1]), speed)

    def press(self, btn="left"):
        self.cmd(f"down {btn}")
        self.down = btn
        self.frame(rep=3)

    def release(self, btn="left"):
        self.cmd(f"up {btn}")
        self.down = None
        self.cmd("wait 3")
        self.frame(rep=4)

    def click(self, label, btn="left"):
        p = self.widget(label)
        if p is not None:
            self.glide(p)
        else:
            self.mouse = (0.0, 0.0)  # pass 1: the probe left the mouse on the button
        self.hold(self.rng.uniform(0.12, 0.25))
        self.press(btn)
        self.release(btn)

    def key(self, k, settle=0.35):
        self.cmd(f"key {k}")
        self.cmd("wait 6")
        self.frame(rep=max(1, round(settle * FPS)))

    def hover_world(self, points, frames):
        """Move the cursor through world points (One ray picks the ray under it)."""
        view = self.view()
        for w in wander(points, frames, self.rng):
            self.world_move(w, view)
            self.frame()

    def drag_light(self, points, frames):
        view = self.view()
        self.glide_world(self.light, view)
        self.hold(0.15)
        self.press()
        for w in wander([self.light] + list(points), frames, self.rng):
            self.world_move(w, view)
            self.frame()
        self.hold(0.1)
        self.release()
        self.light = points[-1]

    def draw_wall(self, a, b, frames=24):
        view = self.view()
        self.glide_world(a, view)
        self.hold(0.15)
        self.press()
        for w in wander([a, b], frames, self.rng):
            self.world_move(w, view)
            self.frame()
        self.release()

    def erase_wall(self, mid):
        view = self.view()
        self.glide_world(mid, view)
        self.hold(0.3)
        self.press("right")
        self.release("right")

    def walk(self, direction, app_frames):
        """Hold an arrow key; every captured frame is 2 app frames (8 world units at 1/60 s)."""
        self.cmd(f"hold {direction} on")
        for _ in range(max(1, app_frames // 2)):
            self.frame()
        self.cmd(f"hold {direction} off")

    def scene(self, name, by_click=True):
        if by_click:
            self.click(SCENE_LABEL[name])
        else:
            self.key(SCENE_KEY[name])
        self.cmd("wait 6")
        self.frame(rep=3)
        self.light = LIGHTS[name]

    def script_text(self):
        return "\n".join(self.L + ["quit"]) + "\n"


# =================================================================================================
# text timing (walkthrough)
# =================================================================================================
CPS = 30.0          # typing speed, characters per second
PARA_GAP = 0.55     # pause between paragraphs
LEAD = 0.35         # pause before typing starts
FADE = 0.5          # text fade-out
GAP = 0.3           # empty column between beats


def char_times(paragraphs):
    """Seconds (from the start of typing) at which each character of each paragraph appears."""
    t = 0.0
    out = []
    for p in paragraphs:
        ts = []
        for i, ch in enumerate(p):
            ts.append(t)
            dt = 1.0 / CPS
            if ch in ".!?" and (i + 1 == len(p) or p[i + 1] == " "):
                dt += 0.28
            elif ch in ",:;":
                dt += 0.12
            t += dt
        out.append(ts)
        t += PARA_GAP
    return out, t - PARA_GAP


def beat_timing(paragraphs):
    """(typing seconds, total seconds) for one beat: lead, typing, a read hold, fade, gap."""
    _, typing = char_times(paragraphs)
    words = sum(len(p.split()) for p in paragraphs)
    hold = max(2.0, words / 3.4 + 1.3 - typing * 0.6)
    return typing, LEAD + typing + hold + FADE + GAP


# =================================================================================================
# the walkthrough
# =================================================================================================
BEATS = [
    ("title", ["Hey! I built a little 2D lighting engine.", "Let me show you what it does."]),
    ("light", ["See the yellow dot? That's a light. The bright part is everything it can see, and the walls cast shadows.",
               "I can grab it and drag it anywhere."]),
    ("live", ["Nothing here is painted on. Every time the light moves, the engine works out again exactly what it can see.",
              "Think of a flashlight in a dark room, except it shines in every direction at once."]),
    ("walls", ["I can draw my own walls too. Just click and drag on the floor.",
               "Changed my mind? Right-click a wall and it's gone."]),
    ("rays", ["So how does it know where the shadows go? It shoots rays.",
              "One ray toward every wall end, every corner, and every spot where two walls cross. Each one stops at the first wall it hits.",
              "Connect all those stopping points and that's the lit area."]),
    ("question", ["The hard part is one question, asked over and over: which wall does this ray hit first?",
                  "The simple way is to check the ray against every single wall. That's the \u201cAll walls\u201d method. The orange walls are the ones it checked."]),
    ("grid", ["The Grid method is my shortcut. It cuts the floor into squares, like a checkerboard, and remembers which walls sit in which square.",
              "A ray then only checks the walls in the squares it passes through. Everything else gets skipped.",
              "It's like looking for your keys. You check the rooms you actually walked through, not the whole house."]),
    ("same", ["Both methods give the exact same picture, down to the last bit. I test that a lot.",
              "The Grid just gets there with way less work."]),
    ("meter", ["This meter keeps score. Every time a ray gets checked against a wall, that's one check.",
               "Here, checking every wall takes about 4,600 checks. The Grid needs about 1,000. Same picture, under a quarter of the work."]),
    ("engine", ["If you like numbers, the Engine view shows everything: rays, checks, squares and timings."]),
    ("forest", ["With a few walls, either method is quick. It gets interesting when there are a lot more.",
                "This is a forest with 1,000 walls. Each little tree is six of them."]),
    ("forest2", ["Checking every wall would take about a million checks every time the light moves.",
                 "The Grid does around 9,000. That's about 1% of the work, because each ray only passes a handful of trees."]),
    ("dungeon", ["This one's my favorite. It's a tiny game level, and the light is the torch this little guy is carrying.",
                 "You walk him around with the arrow keys, and the walls actually stop him."]),
    ("dungeon2", ["A game has to work this out every single frame, sometimes for lots of lights at once.",
                  "Here the Grid does just a few percent of the work, so there's plenty of room to spare."]),
    ("crisscross", ["Now a tricky one. Only 40 walls, but they cross each other all over the place.",
                    "Every crossing is a new corner the light has to aim at, so these 40 walls need more than 400 rays.",
                    "That costs both methods. And long walls run through lots of squares, so the Grid skips less."]),
    ("storeroom", ["And here's where the Grid struggles. Same 1,000 walls as the forest, just crammed into one spot as crates.",
                   "The squares were sized for walls spread across the room. Now a few squares hold tons of walls, and any ray headed that way has to check them all.",
                   "So the Grid does about ten times the forest's work. Still way less than checking everything."]),
    ("exact", ["One more thing I care about: it's exact.",
               "Computers round numbers, and one tiny rounding slip can make the corner of a shadow just vanish. I actually hit that bug.",
               "So every \u201cwhich side of this line is that point on\u201d question gets answered exactly. No guessing, no fudge factor."]),
    ("outro", ["That's the tour! It's all C++, and the math, the tests and the benchmarks are in the repo if you want to dig in.",
               "Thanks for watching!"]),
]


def build_walkthrough(g):
    beats = []                      # (name, paragraphs, start_vf, end_vf)
    rng = g.rng
    g.cmd("set tour off")
    g.cmd("set walls 1000")
    g.cmd("set seed 1")
    g.cmd("wait 4")
    for name, paras in BEATS:
        _, total = beat_timing(paras)
        start = g.vf
        end = start + round(total * FPS)
        budget = end - start
        if name == "title":
            g.move((900, 640))
            g.hold(total)
        elif name == "light":
            g.hold(1.2)
            g.drag_light([(560, 520), (830, 560), (1100, 430), (1240, 250), (900, 190), (700, 330)],
                         budget - g.vf + start - 50)
        elif name == "live":
            g.drag_light([(420, 470), (260, 820), (560, 900), (900, 860), (1150, 760), (610, 290)], budget - 40)
        elif name == "walls":
            g.hold(1.0)
            g.draw_wall((470, 160), (500, 430), frames=34)
            g.hold(3.0)
            g.erase_wall((485, 295))
        elif name == "rays":
            g.hold(0.8)
            g.click("All rays")
            g.hold(1.5)
            g.drag_light([(700, 380), (560, 360), (610, 290)], 150)
        elif name == "question":
            g.click("One ray")
            g.hold(0.4)
            g.click("All walls")
            g.hover_world([(1000, 300), (1150, 520), (900, 700), (400, 650), (300, 200), (1000, 300)], 320)
        elif name == "grid":
            g.click("Grid")
            g.hover_world([(1000, 300), (1150, 520), (900, 700), (400, 650), (300, 200), (1000, 300)], 300)
            g.hold(1.0)
            g.click("Squares")
            g.hold(1.0)
            g.hover_world([(1300, 700), (1000, 900)], 60)
        elif name == "same":
            g.click("Light only")
            g.hold(1.2)
            for _ in range(2):
                g.key("Tab", settle=1.3)
            g.key("Tab", settle=1.3)
            g.key("Tab", settle=0.6)
        elif name == "meter":
            g.key("R", settle=0.5)
            g.light = LIGHTS["demo"]
            p = g.widget("Grid")
            if p is not None:
                g.glide((p[0] + 40, p[1] + 280), speed=0.8)
        elif name == "engine":
            g.click("Engine")
            g.hold(1.0)
            p = g.widget("Engine")
            if p is not None:
                g.glide((p[0] - 60, p[1] + 520), speed=0.6)
            g.hold(2.0)
            g.click("Simple")
        elif name == "forest":
            g.hold(2.0)
            g.scene("forest")
        elif name == "forest2":
            g.drag_light([(780, 520), (930, 610), (1010, 420), (870, 430)], budget - 60)
        elif name == "dungeon":
            g.scene("dungeon")
            g.glide((60, 760))
            g.hold(0.8)
            g.walk("Down", 84)       # out of the hall, into the corridor (y about 616)
            g.hold(0.5)
            g.walk("Right", 130)     # along the corridor to x about 1380
        elif name == "dungeon2":
            g.walk("Right", 22)      # under the shrine door (x about 1468)
            g.walk("Up", 64)         # through the door; the treasure room wall stops him at y 389
            g.hold(0.6)
            g.walk("Left", 18)       # line up with the treasure room door
            g.hold(0.3)
            g.walk("Up", 60)         # through it; the long table stops him
        elif name == "crisscross":
            g.scene("crisscross")
            g.hold(1.5)
            g.click("All rays")
            g.hold(1.2)
            g.drag_light([(650, 660), (560, 740), (590, 710)], 160)
            g.hold(1.0)
            g.click("Light only")
        elif name == "storeroom":
            g.scene("storeroom")
            g.hold(1.5)
            g.click("Squares")
            g.hold(1.0)
            g.drag_light([(860, 440), (1010, 360), (938.969, 504.324)], 200)
            g.hold(1.0)
            g.click("Light only")
        elif name == "exact":
            g.key("R", settle=0.6)
            g.light = LIGHTS["demo"]
            g.drag_light([(640, 330), (720, 420), (900, 470), (1150, 600), (1250, 780)], budget - 70)
        elif name == "outro":
            g.drag_light([(1000, 640), (800, 420), (610, 290)], 150)
        g.hold_until(end)
        beats.append((name, paras, start, max(end, g.vf)))
        if g.vf > end:   # an action ran long: the next beat starts after it
            pass
    return beats


# =================================================================================================
# the short demo
# =================================================================================================
def build_short(g):
    blurbs = []   # (text, start_vf, end_vf)

    def blurb(text, fn):
        s = g.vf
        fn()
        blurbs.append((text, s, g.vf))

    g.cmd("set tour off")
    g.cmd("set walls 1000")
    g.cmd("set seed 1")
    g.cmd("wait 4")
    g.move((900, 640))
    g.hold(0.8)
    blurb("Drag the light", lambda: g.drag_light([(560, 520), (830, 560), (1100, 430), (900, 190), (610, 290)], 120))
    blurb("Draw a wall", lambda: (g.draw_wall((470, 160), (500, 430), frames=28), g.hold(0.8)))
    blurb("Right-click to erase", lambda: (g.erase_wall((485, 295)), g.hold(0.6)))
    blurb("A ray to every corner", lambda: (g.click("All rays"), g.hold(0.6),
                                            g.drag_light([(700, 380), (610, 290)], 60)))
    blurb("All walls: checks everything", lambda: (g.click("One ray"), g.click("All walls"),
                                                   g.hover_world([(1000, 300), (1150, 520), (900, 700)], 75)))
    blurb("Grid: checks nearby squares", lambda: (g.click("Grid"),
                                                  g.hover_world([(900, 700), (1150, 520), (1000, 300)], 75)))
    blurb("Same picture, less work", lambda: (g.click("Light only"), g.key("Tab", 0.9), g.key("Tab", 0.9)))
    blurb("Forest: 1,000 walls", lambda: (g.scene("forest"), g.drag_light([(780, 520), (930, 610), (870, 430)], 80)))
    blurb("Dungeon: walk with the arrows", lambda: (g.scene("dungeon"), g.glide((60, 1000)), g.walk("Down", 84),
                                                     g.walk("Right", 152), g.walk("Up", 64)))
    blurb("Crisscross: lots of corners", lambda: (g.scene("crisscross"), g.click("All rays"), g.hold(1.2),
                                                  g.click("Light only")))
    blurb("Storeroom: the grid's weak spot", lambda: (g.scene("storeroom"), g.click("Squares"), g.hold(1.4),
                                                      g.click("Light only")))
    blurb("Exact shadows, every frame", lambda: (g.key("R", 0.4), setattr(g, "light", LIGHTS["demo"]),
                                                 g.drag_light([(720, 420), (900, 470), (610, 290)], 80), g.hold(1.0)))
    return blurbs


# =================================================================================================
# running the app
# =================================================================================================
def run_app(size, script_path, log_path, timeout=5400):
    with open(log_path, "w") as log:
        p = subprocess.Popen([APP, "--size", size, "--script", script_path], stdout=log, stderr=subprocess.STDOUT)
    return p


def pass1(build, work, size):
    g = Gen(work, {}, capture=False)
    build(g)
    path = os.path.join(work, "pass1.script")
    open(path, "w").write(g.script_text())
    log = os.path.join(work, "pass1.log")
    p = run_app(size, path, log)
    p.wait()
    txt = open(log, errors="replace").read()
    if "FAIL" in txt:
        print(txt[-3000:])
        raise SystemExit("pass 1 failed")
    known = {}
    lines = [l for l in txt.splitlines() if l.startswith("print line")]
    for k, l in enumerate(lines):
        known[str(k)] = dict(kv.split("=", 1) for kv in l.split(": ", 1)[1].split())
    assert len(known) == g.nprobe, (len(known), g.nprobe)
    json.dump(known, open(os.path.join(work, "known.json"), "w"))
    print("pass 1:", g.nprobe, "probes")
    return known


def pass2(build, work, size):
    known = json.load(open(os.path.join(work, "known.json")))
    g = Gen(work, known, capture=True)
    result = build(g)
    path = os.path.join(work, "pass2.script")
    open(path, "w").write(g.script_text())
    raw, jpg = os.path.join(work, "raw"), os.path.join(work, "jpg")
    for d in (raw, jpg):
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
    log = os.path.join(work, "pass2.log")
    p = run_app(size, path, log)
    info, done, t0 = {}, 0, time.time()
    n = len(g.frames)
    while True:
        running = p.poll() is None
        lines = [l for l in open(log, errors="replace").read().splitlines() if l.startswith("print line")]
        while done < len(lines) and done < n:
            bmp = os.path.join(raw, f"f{done:05d}.bmp")
            if not os.path.exists(bmp):
                break
            try:
                im = Image.open(bmp).convert("RGB")
            except OSError:
                break  # still being written
            parts = dict(kv.split("=", 1) for kv in lines[done].split(": ", 1)[1].split())
            info[done] = dict(x=float(parts["mouse_x"]), y=float(parts["mouse_y"]), cursor=parts["cursor"])
            im.save(os.path.join(jpg, f"f{done:05d}.jpg"), quality=94, subsampling=0)
            os.remove(bmp)
            done += 1
        if not running and done >= min(len(lines), n):
            break
        if time.time() - t0 > 5400:
            p.kill()
            raise SystemExit("timeout")
        time.sleep(0.03)
    txt = open(log, errors="replace").read()
    if "FAIL" in txt or p.returncode != 0:
        print(txt[-3000:])
        raise SystemExit(f"pass 2 failed, rc={p.returncode}")
    assert done == n, (done, n)
    meta = dict(frames=g.frames, cursor=info, result=result, vf=g.vf)
    json.dump(meta, open(os.path.join(work, "meta.json"), "w"))
    shutil.rmtree(raw, ignore_errors=True)
    print("pass 2:", n, "unique frames,", g.vf, "video frames,", round(g.vf / FPS, 1), "s")
    return meta


# =================================================================================================
# drawing
# =================================================================================================
_fonts = {}


def font(path, size):
    k = (path, size)
    if k not in _fonts:
        _fonts[k] = ImageFont.truetype(path, size)
    return _fonts[k]


def draw_cursor(img, x, y, shape, down):
    S, box, c = 4, 90, 30 * 4
    layer = Image.new("RGBA", (box * S, box * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    if down:
        col = (255, 190, 90) if down == "left" else (150, 210, 255)
        d.ellipse((c - 20 * S, c - 20 * S, c + 20 * S, c + 20 * S), fill=col + (45,), outline=col + (220,), width=3 * S)
    if shape in ("cross", "move"):
        r = 13 if shape == "cross" else 12
        for wdt, col in ((7, (0, 0, 0, 255)), (3, (255, 255, 255, 255))):
            d.line((c - r * S, c, c + r * S, c), fill=col, width=wdt * S // 2 + 1)
            d.line((c, c - r * S, c, c + r * S), fill=col, width=wdt * S // 2 + 1)
    elif shape == "hand":
        pts = [(0, 0), (0, 21), (5, 16.5), (8.5, 24.5), (12, 23), (8.5, 15.5), (15, 15.5)]
        poly = [(c + px * 1.15 * S, c + py * 1.15 * S) for px, py in pts]
        d.polygon(poly, fill=(255, 255, 255, 255))
        d.line(poly + [poly[0]], fill=(0, 0, 0, 255), width=int(1.8 * S), joint="curve")
    else:
        pts = [(0, 0), (0, 21), (5, 16.5), (8.5, 24.5), (12, 23), (8.5, 15.5), (15, 15.5)]
        poly = [(c + px * 1.15 * S, c + py * 1.15 * S) for px, py in pts]
        d.polygon(poly, fill=(255, 255, 255, 255))
        d.line(poly + [poly[0]], fill=(0, 0, 0, 255), width=int(1.8 * S), joint="curve")
    layer = layer.resize((box, box), Image.LANCZOS)
    img.alpha_composite(layer, (int(round(x)) - 30, int(round(y)) - 30))


def wrap(text, f, width, draw):
    words, lines, cur = text.split(" "), [], ""
    for w in words:
        t = (cur + " " + w).strip()
        if draw.textlength(t, font=f) <= width:
            cur = t
        else:
            lines.append(cur)
            cur = w
    lines.append(cur)
    return lines


def frame_sources(meta):
    """Video frame index -> (unique frame index, its frame record)."""
    out = []
    for i, fr in enumerate(meta["frames"]):
        out.extend([i] * fr["rep"])
    return out


def ffmpeg_writer(path, w, h):
    return subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
                             "-s", f"{w}x{h}", "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-preset", "slow",
                             "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart", path],
                            stdin=subprocess.PIPE)


# ---- walkthrough layout: text column on the left, the app window on the right ----
W, H = 1920, 1080
COL_W = 480
APP_X, APP_Y = 480, 135
BG = (12, 13, 17)
COL_BG = (17, 19, 26)
TEXT = (232, 236, 244)
DIM = (140, 150, 170)
BODY = 30
TITLE = 50
LINE_GAP = 1.36
PARA_SP = 26
TEXT_X, TEXT_Y, TEXT_W = 44, 150, COL_W - 88


def layout_beat(paras, title):
    """Lines per paragraph as (text, font, start_char_index_in_paragraph) with y positions."""
    probe = ImageDraw.Draw(Image.new("RGB", (10, 10)))
    out = []
    y = TEXT_Y
    for pi, p in enumerate(paras):
        f = font(FONT_BOLD, TITLE) if (title and pi == 0) else font(FONT_REG, BODY)
        size = TITLE if (title and pi == 0) else BODY
        idx = 0
        for line in wrap(p, f, TEXT_W, probe):
            out.append((pi, idx, line, f, y))
            idx += len(line) + 1
            y += round(size * LINE_GAP)
        y += PARA_SP
    return out


def text_layer(beat, t):
    """RGBA column overlay for time t (seconds) since the beat started, or None when empty."""
    name, paras, s, e = beat
    title = name == "title"
    times, typing = char_times(paras)
    tt = t - LEAD
    if tt < 0:
        return None
    total = (e - s) / FPS
    fade_start = total - GAP - FADE
    alpha = 1.0 if t < fade_start else max(0.0, 1.0 - (t - fade_start) / FADE)
    if alpha <= 0:
        return None
    layer = Image.new("RGBA", (COL_W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    caret = None
    for pi, idx, line, f, y in layout_beat(paras, title):
        ts = times[pi]
        shown = sum(1 for k in range(idx, idx + len(line)) if ts[k] <= tt)
        if shown == 0:
            continue
        txt = line[:shown]
        d.text((TEXT_X, y), txt, font=f, fill=TEXT + (int(255 * alpha),))
        x = TEXT_X + d.textlength(txt, font=f)
        caret = (x + 3, y + 5, x + 6, y + f.size + 4)
    typing_now = tt < typing
    blink = (int(t * 2.2) % 2 == 0)
    if caret and alpha > 0 and (typing_now or blink):
        d.rectangle(caret, fill=(255, 196, 90, int(255 * alpha)))
    return layer


def compose_walkthrough(meta, work, out_path):
    beats = [tuple(b) for b in meta["result"]]
    src = frame_sources(meta)
    jpg = os.path.join(work, "jpg")
    base = Image.new("RGBA", (W, H), BG + (255,))
    bd = ImageDraw.Draw(base)
    bd.rectangle((0, 0, COL_W - 1, H), fill=COL_BG + (255,))
    bd.line((COL_W - 1, 0, COL_W - 1, H), fill=(40, 45, 58, 255), width=1)
    small = font(FONT_SB, 18)
    bd.text((TEXT_X, 52), "2D LIGHTING ENGINE", font=small, fill=(255, 196, 90, 255))
    bd.text((TEXT_X, 76), "a walkthrough", font=font(FONT_REG, 18), fill=DIM + (255,))
    # soft shadow and a border around the app window
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle((APP_X - 4, APP_Y + 2, APP_X + 1440 + 4, APP_Y + 810 + 10), 10, fill=(0, 0, 0, 150))
    base = Image.alpha_composite(base, sh.filter(ImageFilter.GaussianBlur(10)))
    ImageDraw.Draw(base).rounded_rectangle((APP_X - 2, APP_Y - 2, APP_X + 1440 + 1, APP_Y + 810 + 1), 6,
                                           outline=(52, 58, 74, 255), width=2)
    n = len(src)
    ff = ffmpeg_writer(out_path, W, H)
    cache_i, cache_img = -1, None
    bi = 0
    for v in range(n):
        u = src[v]
        if u != cache_i:
            cache_img = Image.open(os.path.join(jpg, f"f{u:05d}.jpg")).convert("RGBA")
            cache_i = u
        img = base.copy()
        t_video = v / FPS
        while bi + 1 < len(beats) and v >= beats[bi + 1][2]:
            bi += 1
        beat = beats[bi]
        app = cache_img
        if beat[0] == "title":
            # the room waits dimmed under the title, then brightens
            k = min(1.0, max(0.0, (v - beat[2]) / FPS - (beat[3] - beat[2]) / FPS + 1.2) / 1.0)
            app = Image.blend(Image.new("RGBA", app.size, (0, 0, 0, 255)), app, 0.45 + 0.55 * k)
        img.alpha_composite(app, (APP_X, APP_Y))
        c = meta["cursor"][str(u)]
        draw_cursor(img, APP_X + c["x"], APP_Y + c["y"], c["cursor"], meta["frames"][u]["down"])
        tl = text_layer(beat, (v - beat[2]) / FPS)
        if tl is not None:
            img.alpha_composite(tl, (0, 0))
        # whole-video fades
        f_in = min(1.0, t_video / 0.6)
        f_out = min(1.0, (n - 1 - v) / FPS / 0.9)
        k = min(f_in, f_out)
        out = img.convert("RGB")
        if k < 1.0:
            out = Image.blend(Image.new("RGB", out.size, (0, 0, 0)), out, k)
        ff.stdin.write(out.tobytes())
        if v % 300 == 0:
            print(f"  compose {v}/{n}")
    ff.stdin.close()
    ff.wait()


# ---- short demo: the app full width, blurbs top left ----
SW, SH = 1920, 1080


def blurb_layer(text):
    f = font(FONT_SB, 32)
    probe = ImageDraw.Draw(Image.new("RGBA", (10, 10)))
    tw = probe.textlength(text, font=f)
    w, h = int(tw) + 58, 62
    pad = 16
    img = Image.new("RGBA", (w + 2 * pad, h + 2 * pad), (0, 0, 0, 0))
    sh = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle((pad, pad + 4, pad + w, pad + h + 4), 14, fill=(0, 0, 0, 120))
    img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(8)))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((pad, pad, pad + w, pad + h), 14, fill=(10, 12, 18, 225))
    d.rounded_rectangle((pad, pad, pad + 7, pad + h), 3, fill=(255, 196, 90, 255))
    d.text((pad + 30, pad + h // 2), text, font=f, fill=(245, 247, 252, 255), anchor="lm")
    return img


def compose_short(meta, work, out_path):
    blurbs = meta["result"]
    src = frame_sources(meta)
    jpg = os.path.join(work, "jpg")
    n = len(src)
    ff = ffmpeg_writer(out_path, SW, SH)
    layers = {t: blurb_layer(t) for t, _, _ in blurbs}
    cache_i, cache_img = -1, None
    for v in range(n):
        u = src[v]
        if u != cache_i:
            cache_img = Image.open(os.path.join(jpg, f"f{u:05d}.jpg")).convert("RGBA")
            cache_i = u
        img = Image.new("RGBA", (SW, SH), BG + (255,))
        oy = (SH - cache_img.size[1]) // 2
        img.alpha_composite(cache_img, (0, oy))
        c = meta["cursor"][str(u)]
        draw_cursor(img, c["x"], oy + c["y"], c["cursor"], meta["frames"][u]["down"])
        for text, s, e in blurbs:
            # shown while its part plays: 0.35 s slide in from the left with a fade, 0.25 s fade out at the end
            if not (s <= v < e):
                continue
            ti = (v - s) / FPS
            a = min(1.0, ti / 0.35, (e - v) / FPS / 0.25)
            dx = -40 * (1 - minjerk(min(1.0, ti / 0.35)))
            L = layers[text]
            if a < 1.0:
                L = L.copy()
                L.putalpha(L.getchannel("A").point(lambda p: int(p * a)))
            img.alpha_composite(L, (int(14 + dx), oy + 12))
        f_in = min(1.0, v / FPS / 0.4)
        f_out = min(1.0, (n - 1 - v) / FPS / 0.7)
        k = min(f_in, f_out)
        out = img.convert("RGB")
        if k < 1.0:
            out = Image.blend(Image.new("RGB", out.size, (0, 0, 0)), out, k)
        ff.stdin.write(out.tobytes())
    ff.stdin.close()
    ff.wait()


def make_gif(mp4, gif, width=800, fps=15):
    vf = f"fps={fps},scale={width}:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=160:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle"
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", mp4, "-vf", vf, gif], check=True)


def main():
    which = sys.argv[1]
    steps = sys.argv[2:] or ["pass1", "pass2", "compose"]
    work = os.path.join(WORK_ROOT, which)
    os.makedirs(work, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    if which == "walkthrough":
        build, size = build_walkthrough, "1440x810"
    else:
        build, size = build_short, "1920x1080"
    if "pass1" in steps:
        pass1(build, work, size)
    if "pass2" in steps:
        pass2(build, work, size)
    if "compose" in steps:
        meta = json.load(open(os.path.join(work, "meta.json")))
        if which == "walkthrough":
            compose_walkthrough(meta, work, os.path.join(OUT, "walkthrough.mp4"))
        else:
            mp4 = os.path.join(OUT, "demo_short.mp4")
            compose_short(meta, work, mp4)
            make_gif(mp4, os.path.join(OUT, "demo_short.gif"))
    print("done")


if __name__ == "__main__":
    main()
