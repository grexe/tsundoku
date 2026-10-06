# Sounds

`pageturn.wav` is the sound of the fancy mode (a page that turns). It is made from `pageturn-short.m4a`, a shortened version of
"Papier blättern.mp3" by Lokicutter (https://freesound.org/s/596553/, License: Creative Commons 0), converted to 16 bit mono WAV
(`ffmpeg -i pageturn-short.m4a -ac 1 -ar 44100 -c:a pcm_s16le pageturn.wav`). Programs find it as `sounds/pageturn.wav` next to the
application.
