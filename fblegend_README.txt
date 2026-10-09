FOOTBALL LEGENDS CE  (version 2)
================================
A 2D side-view "head soccer" game for the TI-84 Plus CE, styled after
Football Legends (Madpuffers, Poki). Written in C for the CE C/C++
Toolchain (CEdev). One player against the computer, or two players on one
calculator.


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
  3. The program is written to  FBLEGEND\bin\FBLEGEND.8xp  (about 42 KB,
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
    Alpha          supershot (your legend's ability, when the ring is full)

Player 2 (2-player modes):
    APPS           run left
    VARS           run right
    STAT           jump
    PRGM           slide tackle
    (              shoot / kick
    )              supershot

    On the keypad STAT sits above PRGM, with APPS and VARS either side:
    an upside-down T like the arrow keys. ( and ) are two rows below.

Everyone:
    Clear          pause menu (Resume / Restart / Quit to menu)
    Arrows         move in menus; 2nd or Enter confirms; Clear goes back
    Left/Right     on the DIFFICULTY line toggles Normal / Hard
    Del            on the cup bracket screen: abandon the current cup

The same layouts are shown in the CONTROLS screen of the title menu and on
the panels at the start of every match.

Why these keys (keypad ghosting)
The CE keypad is a matrix of 7 groups x 8 bits without diodes. When keys
in two groups share a bit, the two groups become connected and the
calculator can read keys that are not pressed. I simulated that rule for
every realistic situation: each player holding nothing / left / right,
with or without jump, plus nothing / shoot / slide / supershot, both
players at the same time (24 x 24 = 576 combinations). The game's own key
decoder was run on the "ghosted" keypad state and compared with the real
presses, and phantom presses of CLEAR (pause) and of the panic keys were
counted as errors too.
    This layout:                       0 of 576 combinations go wrong.
    Number pad (4 6 8 5 1 0) for P2: 156 of 576 go wrong (for example
    player 1 jumping while player 2 jumps and slides reads as a phantom
    RIGHT for player 1), because the number keys share bits with the
    arrow keys.
The P2 layout was picked by an exhaustive search over ~13 million key
assignments, then by comfort. Holding several keys never loses a real
press in this model; only phantom presses are possible, and with these
layouts they never change what the game does.


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
GG). After a goal: screen shake, a GOAL! banner, confetti, the crowd jumps,
then the ball drops on the side of the team that conceded.

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
  - Fixed 30 Hz timestep from clock(); up to 3 physics steps run if a
    frame is late, so game speed never depends on drawing speed.
  - Double buffered. The stadium is drawn procedurally (no 76.8 KB
    picture): roof arch per row, a cached 320x30 sky/roof strip, crowd
    tiles (3 tiers x 2 frames, generated at start-up), perspective pitch
    stripes merged into rectangles, pre-computed line pixels, platforms,
    nets from a generated hex tile, soil. Each frame only the rectangles
    that moving things covered two frames earlier are redrawn; the HUD
    widgets are redrawn only when they change or something passes over
    them. The two legends' heads and the ball are pre-rendered into
    sprites for each match.
  - Crowd arms waving, flags, fire, the ready glow and menu highlights are
    animated by rewriting palette entries: no drawing at all.
  - Static data (BSS): about 43 KB (limit about 69 KB).
  - Game feel: hit-pause on big impacts, screen shake on goals and power
    shots, ball squash/stretch and spin, landing squash, star sparks on
    hard shots, flame trails on power shots, sliding panels and shutter
    transitions in the menus.


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
      * 180,000 frames of random key presses on every screen (both
        players at once): no crash, no memory error, no undefined
        behaviour;
      * a check after EVERY match frame that the redrawn stadium is
        pixel-identical to a freshly drawn one (about 2.2 million checks,
        0 differences), plus checks for 24-bit overflow, text drawn off
        the screen and NoClip calls outside the screen (all 0);
      * AI-vs-AI matches (over 5,000 in the final test run, including
        the balance runs): every match ends, golden goal always resolves, about 7
        goals per 60 s match. The left side wins about 51.6%;
      * the ball never stays stuck on a platform, ramp or crossbar (a
        "resting for more than 2 s next to a platform" detector reported
        0 in the final runs);
      * ability balance: every ordered pair of the 10 legends, 50 matches
        each (4,500 matches). Overall win rates 45.7% (Magnet) to 54.6%
        (Heavy Ball); every pairing between 36% and 64%. No ability wins
        a match by itself;
      * Hard vs Normal: Hard won 557 of 800 matches (from either side);
      * 6 cups (wins and knock-outs), leaving a cup, quitting the
        program and continuing the saved cup;
      * 2-player matches and best-of series with both players mashing
        their keys (keypad ghosting simulated on every scan);
      * the ghosting check described in section 2 (0 of 576 wrong);
      * the panic exit typed on 12 different screens (title, team
        select, 1-player match, pause, results, cup bracket, records,
        controls, 2-player menu, 2-player match, series and message
        screens): it quit in the same key scan every time, called
        gfx_End() and wrote nothing;
      * screenshots of every screen and every ability, checked by eye.
    Estimated drawing cost (see Limits): about 0.5 million cycles per
    match frame on average, worst about 1.7 million; 0.02% of frames
    were estimated above the 1.6 million available at 30 FPS.
Limits:
  - It has NOT been run on a real calculator or an emulator (no ROM was
    available), so the frame rate was NOT measured. The harness counts
    the drawing work of every match frame (graphx calls, pixels filled,
    sprite pixels, text) and converts it with a deliberately pessimistic
    cost model; that ESTIMATE is given above. It suggests 30 FPS with
    headroom, but treat it as an estimate.
  - The calculator has no speaker: the "beep" in the last 5 seconds is a
    flash and pulse of the clock.
  - Text uses the built-in 8x8 font; big numbers use drawn segments.
  - "Fade out" of the control panels is a slide up (8-bit palette mode has
    no transparency).
  - The keypad ghosting model is the standard diode-less matrix rule; real
    hardware was not available to confirm it.
