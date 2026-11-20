# Walkthrough captions: edit the text, then hand it back

Rules:
- Each `## beat` is one screen of typed text. Keep the beat names (the on-screen action is tied to them).
- A blank line between paragraphs = a new paragraph in the video. Add or remove paragraphs freely.
- The "On screen" line is only a reminder of what the app is doing during that beat. Don't edit it.
- Length matters: the beat's duration is computed from the text length (typing speed 30 characters per second, plus a read time). Longer text = a longer beat, and the on-screen action is stretched to fit. Very short text can feel rushed against a long action.
- Typing the quote marks as plain " or as curly quotes both work.

---

## title
On screen: app idle, light sitting still.

Hello, and welcome to the lighting engine demo!

Let's walk through what it does together.

## light
On screen: light dragged around a room with walls.

That yellow ball is our light source. The bright area is everything that isn't blocked, just like a real light source.

You can pick it up and drag it anywhere in the room, even through walls because I didn't want to add collision.

## live
On screen: light keeps moving around, shadows update.

Every time the light moves, the engine recalculates exactly what it can see and what it can't.

## walls
On screen: a wall is drawn by click-drag, then right-click erases it.

You can also draw your own walls by clicking and dragging on the screen.

If you change your mind, right-clicking a wall removes it.

## rays
On screen: "All rays" is switched on, rays fan out, light moves a little.

It casts rays to figure out where the shadows are cast.

It sends one ray toward every wall end, corner, and point where two walls cross. Each ray stops at the first wall it hits.

Connecting all of those "stopping points" gives us the lit area!

## question
On screen: "One ray" then "All walls" method; the checked walls light up orange as the mouse sweeps around.

The hard part comes down to one question: which wall does this ray hit first?

The simple answer is to check the ray against every single wall. That's what the "All walls" method does, and the orange walls are the ones it checks (all of them :0)

## grid
On screen: "Grid" method on, then the "Squares" overlay shown.

The Grid method is the "shortcut" I chose to test. It divides the floor into squares, like a checkerboard, and keeps track of which walls sit in which square.

A ray then only checks the walls in the squares it passes through, skipping everything else.

It's kind of like looking for your keys or something. You would only check the rooms you actually walked through instead of the whole house.

## same
On screen: "Light only" view, Tab flips between the two methods; the picture doesn't change.

Both methods produce the exact same picture but the Grid gets there with far fewer operations.

## meter
On screen: scene reset, cursor moves to the check counter.

This meter keeps the score for the methods. Every time a ray is checked against a wall, that counts as one check.

In this scene, checking every wall takes about 4,600 checks, while the Grid needs about 1,000. You get the exact same with under a quarter of the work.

## engine
On screen: "Engine" panel opens (rays, checks, squares, timings).

If you like seeing all the numbers like a little weirdo, the Engine view shows all of it. (I like the numbers)

## forest
On screen: forest scene, 1,000 walls.

With only a few walls, either method is relatively fast. The real difference shows up when there are many more.

This is a forest (please just imagine you're looking from the top down) with 1,000 walls, and each small tree is made of six of them.

## forest2
On screen: forest, comparing All walls vs Grid counts.

Checking every wall would take about a million checks each time the light moves.

The Grid takes around 9,000, only about 1% of the work, because each ray only passes a handful of trees.

## dungeon
On screen: dungeon scene, character walks with arrow keys, torch light follows.

This one is pretty cool, I even added collision this time!

You can walk the character around with the arrow keys. Alas, he is doomed to wander this labyrinth for all eternity. At least he has dynamic lighting!

## dungeon2
On screen: dungeon, work numbers shown.

A game has to do this calculation on every frame, often for many lights at once from different directions.

Here, the Grid does only a few percent of the work, which means there's plenty of room to spare.

## crisscross
On screen: crisscross scene, 40 crossing walls, 400+ rays.

Here's where both methods have a tough time. There are only 40 walls, but they cross each other a lot.

Every time they cross each other light has to aim at another corner, so these 40 walls need more than 400 rays.

That costs both methods more, and since the long walls pass through many squares, the Grid can't skip as many.

## storeroom
On screen: storeroom scene, 1,000 walls crammed into crates in one spot.

This is where the Grid struggles the most. It's the same 1,000 walls as the forest, but packed into one spot as crates.

The squares were sized for walls spread across the room. Now a few squares hold a large number of walls, and any ray headed that way has to check all of them.

As a result, the Grid does about ten times the work it did in the forest. Even still, it's better than checking everything (hard to get worse to be honest).

## exact
On screen: close-up shadow corners / exactness demo.

I ran into a bug while testing this where a small rounding error could make the corner of a shadow disappear. The values for the light, wall end, and the corner were collinear in decimal, but not binary since they can't be stored exactly. That caused the steps to disagree and drop the polygon vertex.

I ended up replacing the rounded math with an exact “which side of this line is the point on?” test, and use that one answer for every decision, so the steps can’t disagree about the same corner.

## outro
On screen: final scene, fade out.

That's the tour. It's all written in C++, and the math, tests and benchmarks are in the repo if you'd like to look deeper (shoutout Claude for writing the docs (I still checked them don't worry))

Thank you for watching!
