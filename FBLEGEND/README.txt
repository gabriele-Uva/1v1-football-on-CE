FOOTBALL LEGENDS CE  (version 3)
================================
A 2D side-view "head soccer" game for the TI-84 Plus CE, styled after
Football Legends (Madpuffers, Poki). Written in C for the CE C/C++
Toolchain (CEdev). One player against the computer, or two players on one
calculator.

What is new in version 3: much faster drawing, now up to 60 frames per
second (version 2 lagged on real calculators); player 1's supershot is on
MODE; player 2 plays on the number keys 1 2 3 5 with + and -.


1. BUILDING (Windows)
---------------------
Folder layout:

    FBLEGEND\makefile
    FBLEGEND\src\main.c
    FBLEGEND\README.txt   (this file, not needed to build)

IMPORTANT: Windows hides file extensions by default. Make sure the source
file is really called "main.c" and NOT "main.c.txt", and the makefile is
called "makefile" with no extension at all. In Explorer: View > Show >
File name extensions. If you copied the .txt versions, rename them:
    fblegend_main_c.txt   ->  FBLEGEND\src\main.c
    fblegend_makefile.txt ->  FBLEGEND\makefile

Steps:
  1. Install CEdev (https://ce-programming.github.io/toolchain/).
  2. Open the CEdev command prompt (cedev.bat), cd into the FBLEGEND
     folder and type:   make
  3. The program is written to  FBLEGEND\bin\FBLEGEND.8xp  (about 43 KB,
     archived and compressed).
  4. Send FBLEGEND.8xp to the calculator with TI Connect CE.
  5. The calculator also needs the CE C libraries (clibs.8xg: graphx,
     keypadc, fileioc) from the CE Programming "libraries" release.
  6. Run it from the Programs menu (or with Cesium / arTIfiCE if your OS
     version needs it for ASM programs).


2. CONTROLS
-----------
Player 1:
    Left / Right   run
    Up             jump (hold to keep jumping)
    Down           slide tackle
    2nd            shoot / kick
    MODE           supershot (your legend's ability, when the ring is full)

Player 2 (2-player modes), on the number keys:
    1              run left
    3              run right
    5              jump
    2              slide tackle
    +              shoot / kick
    -              supershot

    5 sits above 2, with 1 and 3 either side: an upside-down T like the
    arrow keys. + is right next to 3 and - right next to 6, so the
    right-hand fingers rest on + and -.

Everyone:
    Clear          pause menu (Resume / Restart / Quit to menu)
    Arrows         move in menus; 2nd or Enter confirms; Clear goes back
    Left/Right     on the DIFFICULTY line toggles Normal / Hard
    Del            on the cup bracket screen: abandon the current cup

The same layouts are shown in the CONTROLS screen of the title menu and on
the panels at the start of every match.

Keypad ghosting (read this if two players share the calculator)
The CE keypad is a matrix of 7 groups x 8 bits without diodes. When keys
in two groups share a bit, the two groups become connected and the
calculator can read keys that are not pressed. The number keys share
bits with the arrow keys (1, 2, 3 and + with LEFT; 5 and - with RIGHT),
so with this layout some combinations DO produce phantom keys. I
simulated every realistic situation: each player holding nothing / left
/ right, with or without jump, plus nothing / shoot / slide / supershot,
both players at the same time (24 x 24 = 576 combinations), and ran the
game's own key decoder on the "ghosted" keypad state:
    phantom keys appear in 209 of the 576 combinations;
    0 of the 576 change what the game does (no phantom CLEAR or panic key
    either).
How: the extra keys that appear are mostly ones the game does not use
(4, 6 ...). The one that matters is a phantom opposite arrow for player 1
when player 2 holds 5 and 2 together. The decoder handles that: when it
reads LEFT and RIGHT at once, it keeps the direction that was already
held.
What is still possible: if player 2 is ALREADY holding 5 and 2 together
and player 1 only then presses Left or Right, player 1 does not start
running until player 2 lets go of one of them. Holding 5 and 2 together
is useless anyway (jump wins over slide), so just don't.


3. HOW TO PLAY
--------------
Side view of a stadium; each goal stands on a raised grass platform with a
short ramp up to it. A goal counts when the ball has completely crossed the
goal line below the crossbar. You always face the opponent's goal.

  - Shoot on the ground: a low, straight drive (rolls up the ramp).
  - Shoot while jumping: a lifted volley, or a header driven down at goal
    if the ball is at your head.
  - Jumping into the ball: a header that always goes forward.
  - Slide: a fast low dash that takes the ball and knocks the opponent
    over. Afterwards you lie on the ground for a moment.
  - Landing on the opponent's head bounces you up again.

SUPERSHOT: the round icon in the bottom corner fills its ring each time
you touch the ball. When it is full the ball in the icon catches fire and
the ability name flashes. Press your supershot key: abilities that need
the ball fire at once when it is close, otherwise your legend "charges"
(glowing ring, up to 3 s) and the next touch fires it. Timed abilities
show their remaining time on the ring in cyan.

The 10 legends and their abilities
  BRAZIL       FERA     FIREBALL     blazing, dead-straight shot at goal
  ARGENTINA    EL PIBE  TELEPORT     ball warps to the rival's goal mouth
  GERMANY      KAISER   FREEZE       rival frozen solid for 1.5 s
  NETHERLANDS  ROCKY    ROCKET JUMP  huge jump; the next header is smashed
                                     down at the goal
  FRANCE       LE ROI   GIANT        grows for 5 s: bigger head and body,
                                     harder kicks, cannot be knocked over
  SPAIN        MATADOR  MAGNET       pulls the ball towards you for 3 s
  PORTUGAL     CICLONE  CLONE        a shadow twin plays beside you, 4 s
  ITALY        MURO     WALL         a barrier guards your goal for 4 s
  ENGLAND      LION     SPEED BOOST  run 60% faster for 5 s
  MEXICO       CHICO    HEAVY BALL   iron ball, bowls over the keeper
(Fictional legends; only country names and kit colours.)
A power shot (fireball, heavy ball, rocket smash) that hits the defender
knocks him over, but a well-timed kick clears it.

Match: two halves of 30 seconds. The big number at the top counts down
the seconds left in the half; it flashes red and pulses in the last 5
seconds. A level score at full time goes to GOLDEN GOAL (the clock shows
GG). After a goal: a GOAL! banner, confetti, the crowd waves wildly, then
the ball drops on the side of the team that conceded.

Modes
  QUICK MATCH   pick a legend, play the computer.
  TOURNAMENT    8-team knockout (QF, SF, final) against 7 computer teams
                chosen from the other 9. Lose once and it is over. Other
                ties are simulated. Progress is saved after every match:
                leave from the bracket with Clear and choose CONTINUE CUP
                later. The computer gets a little sharper each round.
  2 PLAYERS     QUICK FRIENDLY (one match), BEST OF 3, BEST OF 5, and
                P1 VS CPU (player 1 against the computer, same controls).
                Both players pick their own legend; if both pick teams
                with the same shirt, player 2 wears the away kit.
  DIFFICULTY    Normal or Hard. Hard reacts every 2 ticks instead of 8,
                positions itself more accurately, shoots from further out,
                slides more often (only when the tackle will land) and
                tries every header.
  CONTROLS      both key layouts.
  RECORDS       matches, wins, losses, goals, biggest win, best streak,
                cups entered and won (2-player matches are not counted).


4. PANIC EXIT (secret)
----------------------
Press these five keys one after another, from ANY screen (menus, match,
pause, 2-player match, results ...):

        Y=   WINDOW   ZOOM   TRACE   GRAPH

(the top row of keys, left to right). The game quits at once to the home
screen: no confirmation, nothing is saved. None of these keys is used
anywhere else in the game, so normal play cannot trigger it. It works by
remembering the last five key presses (each new press counts once; held
keys do not repeat). To change it, edit PANIC_SEQ near the top of main.c.
It is not shown on any game screen.


5. SAVING
---------
Records, the difficulty and an unfinished cup are kept in the AppVar
FBLGSAVE (54 bytes). It is written to RAM after every match and archived
when you quit through the menu. The panic exit does not save. Version 1
saves are ignored (records start again).


6. TECHNICAL NOTES
------------------
  - Pure C, using graphx, keypadc, fileioc, tice.h and the standard
    library (setjmp/longjmp for the panic exit). No floating point:
    physics is integer fixed point (1 pixel = 64 units); products that can
    exceed 24 bits are done in long.
  - Physics and AI run on a fixed 30 Hz timestep from clock() (up to 3
    steps catch up if a frame is late, so game speed never depends on
    drawing speed). The screen is drawn twice per step: right after a step
    (players and ball half-way between the last two positions) and half a
    step later (newest positions). That gives 60 frames per second of
    smooth movement while the physics stays exactly as tested; the screen
    swap waits for the display, so the rate is capped by its refresh.
  - Double buffered. Each frame only the rectangles that moving things
    covered two frames earlier are restored, then things are drawn.
  - Why version 2 lagged, and what version 3 changed:
      * The stadium behind every moving thing was redrawn procedurally
        (roof rows, crowd tiles, stripes, lines ...): dozens of graphx
        calls per rectangle. Now the finished stadium is drawn once at
        start-up and stored in a compact cache (16-pixel pieces, identical
        pieces stored once: 897 pieces; 27 KB with spare room). Restoring a
        rectangle is a few memcpy/memset calls per row.
      * Screen shake redrew the whole screen for several frames: removed.
      * The crowd "jump" redrew the whole stand every 4 ticks during goal
        celebrations: the crowd now waves through palette animation only.
      * READY / GOAL! / HALF TIME were redrawn every frame (text printed 5
        times for the outline): now they are a HUD widget, redrawn only
        when they change or something crosses them, with a 2-print shadow.
      * Heads were 44x46 sprites even for normal size; now cropped (about
        27x29). Frozen, clone and giant heads were drawn shape by shape
        every frame; now they come from the cached sprites.
      * Tighter dirty rectangles, no particles over the HUD (they forced a
        whole widget redraw), the control panels no longer slide.
  - Crowd arms waving, flags, fire, the ready glow and menu highlights are
    animated by rewriting palette entries: no drawing at all.
  - Static data (BSS): about 44 KB (the tiles and tables used to draw the
    stadium at start-up share memory with the cache).
  - Game feel: hit-pause on big impacts, ball squash/stretch and spin,
    landing squash, star sparks on hard shots, flame trails on power
    shots, sliding panels and shutter transitions in the menus.


7. TESTING THAT WAS DONE, AND HONEST LIMITS
--------------------------------------------
Done:
  - Built with the real eZ80 toolchain, CEdev v11.2, v12.1, v13.0 and a
    current nightly, with -Wall -Wextra -Oz: no errors and no warnings
    from the code. Also ez80-clang -fsyntax-only -Wall -Wextra -Wshadow
    against the real CEdev headers: clean.
  - The same main.c compiled on a PC against stand-in headers with a fake
    framebuffer, AddressSanitizer + UndefinedBehaviorSanitizer, and the
    keypad ghosting rule applied to every simulated key scan:
      * about 250,000 frames of random key presses on every screen (both
        players at once, pausing, restarting, quitting): no crash, no
        memory error, no undefined behaviour;
      * at start-up, the cached stadium is compared with the procedurally
        drawn one: identical;
      * a check after EVERY match frame that the restored stadium is
        pixel-identical to the original (about 5 million checks in the
        final run, 0 differences), plus checks for 24-bit overflow, text
        drawn off the screen and NoClip calls outside the screen (all 0);
      * AI-vs-AI matches (over 4,500 in the final run): every match ends,
        golden goal always resolves, the ball never stays stuck on a
        platform, ramp or crossbar;
      * ability balance: every ordered pair of the 10 legends, 50 matches
        each (4,500 matches). Overall win rates 43.7% (Speed Boost) to
        55.6% (Heavy Ball); every pairing between 35% and 65%;
      * Hard vs Normal: Hard won 566 of 800 matches (from either side);
      * 6 cups (wins and knock-outs), leaving a cup, quitting the
        program and continuing the saved cup;
      * 2-player matches and best-of series with both players mashing
        their keys (keypad ghosting simulated on every scan);
      * the ghosting check described in section 2;
      * the panic exit typed on 12 different screens: it quit in the same
        key scan every time, called gfx_End() and wrote nothing;
      * screenshots of every screen and every ability, checked by eye.
  - Speed. The harness counts the drawing work of every match frame
    (graphx calls, rows, pixels filled, sprite pixels, characters,
    memcpy/memset calls and bytes) and converts it with a deliberately
    pessimistic eZ80 cost model, plus 200,000 cycles for the physics/AI
    step on frames that follow one. Result (ESTIMATE):
      version 2: about 0.9 million cycles of drawing per frame on
                 average, so 30 FPS or less with frequent drops (the lag
                 you saw);
      version 3: about 0.4 million cycles per frame on average including
                 the game logic, against 0.8 million available for 60 FPS
                 at 48 MHz. About 1% of frames are estimated above that
                 budget (mostly the first frames of the GOAL! banner and
                 the start of a match); those show for 1/30 s instead of
                 1/60 s.
Limits:
  - It has NOT been run on a real calculator or an emulator (no ROM was
    available), so the frame rate was NOT measured. The numbers above are
    estimates from counted work. Please tell me how it feels on your
    calculator.
  - The calculator has no speaker: the "beep" in the last 5 seconds is a
    flash and pulse of the clock.
  - Text uses the built-in 8x8 font; big numbers use drawn segments.
  - The keypad ghosting model is the standard diode-less matrix rule; real
    hardware was not available to confirm it.
