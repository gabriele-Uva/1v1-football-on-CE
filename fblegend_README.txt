FOOTBALL LEGENDS CE
===================
A 2D side-view "head soccer" game for the TI-84 Plus CE, in the style of
Football Legends (Madpuffers). Written in C for the CE C/C++ Toolchain
(CEdev). One player against the computer, or two players on one calculator.


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
  3. The program is written to  FBLEGEND\bin\FBLEGEND.8xp
  4. Send FBLEGEND.8xp to the calculator with TI Connect CE.
  5. The calculator also needs the CE C libraries (clibs.8xg: graphx,
     keypadc, fileioc). If they are missing the calculator says so when you
     start the game. Get them from the CE Programming "libraries" release on
     GitHub and send clibs.8xg the same way.
  6. Run it from the Programs menu (or with Cesium / arTIfiCE if your OS
     version needs it for ASM programs).

The program is built ARCHIVED and COMPRESSED (about 24.5 KB on the
calculator).


2. CONTROLS
-----------
Player 1 (always):
    Left / Right   run
    Up             jump (hold to keep jumping)
    Down           slide tackle
    2nd            shoot / kick
    Alpha          supershot (when your power bar is full)

Player 2 (Friendly mode only):
    ,  (comma)     run left
    )              run right
    PRGM           jump
    (              slide tackle
    APPS           shoot / kick
    VARS           supershot

    On the keypad these sit together: the "APPS PRGM VARS" row is the
    action row and the ", ( )" row two rows below it is left/slide/right.

Everyone:
    Clear          pause menu (Resume / Restart / Quit to menu)
    Arrows         move in menus
    2nd or Enter   confirm in menus, Clear = back
    Left/Right     on DIFFICULTY in the title menu toggles Normal/Hard
    Del            on the cup bracket screen: abandon the current cup

Why player 2 is not on the number keys (honest note about key ghosting)
The CE keypad is a matrix of 7 groups x 8 bits. If three keys that form
three corners of a rectangle in that matrix are held, the fourth corner can
read as pressed too ("ghosting"). The number keys 1-9 sit in the same
matrix bits as the arrow keys, so for example player 1 holding Right+Up
while player 2 holds 5 would make the game see 8 as well. The keys chosen
for player 2 ( , ( ) APPS PRGM VARS ) share no matrix row and no matrix
bit with ANY of player 1's keys, so the two players can never ghost into
each other, whatever they hold.
Inside player 2's own six keys a phantom key is still possible, but only
for some three-key combinations. Everything you normally hold is safe:
direction + jump, direction + shoot, jump + shoot, direction + jump +
shoot (running volley), direction + slide. Two cases are handled by the
game (a phantom slide while jumping is ignored, because jump always wins
over slide). These rarer triples can still misfire:
    slide + left  + shoot    -> may also read jump
    slide + left  + jump     -> may also read shoot
    slide + right + jump     -> may also read supershot
    slide + right + super    -> may also read jump
    slide + jump  + shoot    -> may also read left
    slide + jump  + super    -> may also read right
    shoot + super + left     -> may also read right (you stop moving)
    shoot + super + right    -> may also read left  (you stop moving)
In short: tap slide on its own (or with one direction), and do not press
shoot and supershot at the same moment.
Player 1's keys can never ghost among themselves.


3. HOW TO PLAY
--------------
Side view, two big-headed legends, one goal at each end. You always face
the opponent's goal, so every kick, volley and header goes towards it.

  - Shoot on the ground: a low, straight drive.
  - Shoot while jumping: a lifted volley, or (if the ball is at your
    head) a powerful header driven down at goal.
  - Just jumping into the ball: a header that always goes forward.
  - Slide: a fast low dash that takes the ball and knocks the opponent
    down if it hits them. Afterwards you lie on the ground for a moment.
  - Landing on the opponent's head bounces you up again.

SUPERSHOT: the bar under your player (and in the scoreboard) fills a
little each time you touch the ball. When it is full it flashes. Press the
supershot key: if the ball is close it fires at once, otherwise your
legend "charges" (glowing ring, up to 3 seconds) and the next touch fires
it. Freeze needs no ball and fires immediately. The bar then resets.
A power shot that hits the opponent knocks them over, but a well-timed
kick by the defender clears it.

Match: two halves of 30 seconds with a count-up clock (0:00-0:30, then
0:30-1:00). In the last 5 seconds of each half the clock flashes and
pulses once per second. A level score at full time goes to GOLDEN GOAL:
the next goal wins. After a goal there is a short celebration (crowd goes
wild, confetti), then the ball is dropped on the side of the team that
conceded.

Modes
  QUICK MATCH   pick a team, play the computer.
  TOURNAMENT    8-team knockout: quarter-final, semi-final, final against
                the 7 computer teams. Lose once and it is over. Other ties
                in the bracket are simulated. Progress is saved after every
                match: leave from the bracket screen with Clear and choose
                CONTINUE CUP later, even after turning the calculator off.
  FRIENDLY 2P   two players on one calculator. Picking the same team
                gives player 2 the away kit. Not counted in the records.
  DIFFICULTY    Normal or Hard (Left/Right or 2nd on the menu line).
                Hard reacts every 2 ticks instead of 8, positions itself
                more accurately, shoots from further out (170 px vs
                110 px), slides more often but only when the tackle will
                land, and tries every header chance. In tests Hard beat
                Normal in 141 of 200 AI-vs-AI matches. In a cup the
                computer also reacts a little faster each round.
  RECORDS       matches, wins, losses, goals, biggest win, best winning
                streak, cups entered and won.

The 8 teams and their legends
  BRAZIL       FERA      FIREBALL   blazing shot, dead straight at goal
  ARGENTINA    EL PIBE   TELEPORT   ball warps to the rival goal mouth
  GERMANY      KAISER    FREEZE     rival frozen solid for 1.5 seconds
  NETHERLANDS  ROCKY     ROCKET     fast, high-arc rocket shot
  FRANCE       LE ROI    LIGHTNING  zig-zag thunder strike at goal
  SPAIN        MATADOR   METEOR     ball soars up, then crashes down
  PORTUGAL     CICLONE   TORNADO    wavy twister that floors defenders
  ITALY        MURO      MEGA BALL  giant ball that bowls over keepers
(Fictional legends; country names and kit colours only.)


4. SAVING
---------
Records, the difficulty setting and an unfinished cup are kept in the
AppVar FBLGSAVE (54 bytes). It is written to RAM after every match and
moved to the archive when you quit through the menu, so it survives a RAM
clear. If you leave the program any other way (battery pull, crash) the
last RAM copy is used if it is still there. Delete FBLGSAVE in the memory
manager to reset everything.


5. TECHNICAL NOTES
------------------
  - Pure C, using graphx, keypadc, fileioc, tice.h and the standard
    library. No floating point anywhere: physics uses integer fixed point
    (1 pixel = 64 units). int is 24 bits on the eZ80, so all products that
    can get large are computed in long.
  - Fixed 30 Hz timestep driven by clock() (the 32 kHz hardware timer):
    game speed does not depend on drawing speed. If a frame is ever late,
    up to 3 physics steps run before the next draw.
  - Double buffered (gfx_SetDrawBuffer / gfx_SwapDraw). The stadium is
    drawn once into both buffers. Each frame only the rectangles that the
    players, ball, particles and text covered two frames earlier are
    restored, then the moving things are drawn. The scoreboard is only
    redrawn when something on it changes.
  - Static data (BSS) is about 10.4 KB; the generated crowd and advert
    sprites are the largest part (7.5 KB).
  - Custom 53-colour palette, crowd tiles and advert boards generated at
    start-up, heads drawn with graphx primitives (8 hair styles, beards,
    skin tones), particles for every supershot, goal celebration with a
    cheering crowd and confetti.


6. TESTING THAT WAS DONE, AND HONEST LIMITS
--------------------------------------------
Done:
  - Compiled and linked with the real eZ80 toolchain, CEdev v11.2, v12.1,
    v13.0 and a current nightly, with -Wall -Wextra -Oz: no errors and
    no warnings from the code. (Only the nightly toolchain prints a
    linker note about its own crt0.o / libc objects missing a
    .note.GNU-stack section; that is a toolchain packaging note, not the
    game.) Also checked with ez80-clang -fsyntax-only -Wshadow -Wpedantic
    against the real headers.
  - The same main.c was compiled on a PC against stand-in headers with a
    fake 320x240 framebuffer, run with AddressSanitizer and
    UndefinedBehaviorSanitizer, and driven by a script:
      * 463,000 frames of random key presses on every screen
        (menus, pause, matches, P1 and P2 keys at once): no crashes, no
        memory errors, no undefined behaviour;
      * several hundred full AI-vs-AI matches while tuning, and in the
        final run 40 more (Normal and Hard): every match ends, nothing
        gets stuck, golden goal always resolves (longest seen: about
        16 s), the left and right sides are balanced (final run 21-19,
        goals 142-144), about 7 goals per match;
      * Hard vs Normal head to head: Hard won 141 of 200;
      * 8 complete tournaments (won one, knocked out in every round),
        including leaving mid-cup, quitting the program, restarting and
        continuing the saved cup;
      * a check after EVERY frame that the restored background is
        pixel-identical to a freshly drawn stadium (no smears or leftover
        pixels), a 24-bit overflow check on positions and speeds, a check
        that no text is drawn off-screen, and that no "NoClip" graphx call
        goes outside the screen;
      * screenshots of every screen were checked by eye.
Limits:
  - It has NOT been run on a real calculator or an emulator: no ROM was
    available. The frame rate was therefore NOT measured. From the
    counted drawing work per frame (pixels, calls, characters) and a
    conservative cost model, a typical match frame is estimated at about
    0.3-0.6 million CPU cycles and the worst seen at about 0.8 million,
    against 1.6 million available per frame at 30 FPS on the 48 MHz eZ80.
    That suggests a steady 30 FPS, but it is an estimate, not a
    measurement. Physics always runs at 30 Hz either way.
  - The TI-84 Plus CE has no speaker, so the "beep" in the last five
    seconds is visual: the clock flashes and its box pulses white once
    per second.
  - Text uses the built-in 8x8 graphx font in monospace mode.
  - The computer opponent is a state machine (defend, hold a line,
    chase, attack, header, volley, slide, supershot). It is meant to be
    beatable on Normal; it is not a perfect player.
