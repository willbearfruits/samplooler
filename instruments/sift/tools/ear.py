#!/usr/bin/env python3
"""An ear for SIFT's tests: how many of Daisy Bell's words a speech recogniser hears in a WAV.

    python3 tools/ear.py out.wav [more.wav ...]

Each file is taken to 16 kHz with ffmpeg, given to whisper.cpp (whisper-cli, the large-v3-turbo model in
~/.local/share/whisper/), and the words it writes are matched, in order, against the first two lines of the chorus
("daisy daisy give me your answer do i'm half crazy all for the love of you"), the loop starting anywhere in them.
Prints one line per file:
<words heard> <words in the lines> <what it heard>. Nothing is sent anywhere: whisper runs on this machine.
"""
import os, re, subprocess, sys, tempfile

LYRIC = "daisy daisy give me your answer do im half crazy all for the love of you".split()
MODEL = os.path.expanduser('~/.local/share/whisper/ggml-large-v3-turbo.bin')

def lcs(a, b):
    t = [[0] * (len(b) + 1) for _ in range(len(a) + 1)]
    for i in range(len(a)):
        for j in range(len(b)):
            t[i + 1][j + 1] = t[i][j] + 1 if a[i] == b[j] else max(t[i][j + 1], t[i + 1][j])
    return t[len(a)][len(b)]

def heard(text):
    words = re.sub(r"[^a-z ]", "", text.lower().replace("'", "")).split()
    # the lyric's words heard in their order (the longest common subsequence), the loop starting anywhere in it
    return max(lcs(LYRIC[k:] + LYRIC[:k], words) for k in range(len(LYRIC)))

def listen(path):
    with tempfile.TemporaryDirectory() as d:
        w16 = os.path.join(d, 'in.wav')
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', path, '-ar', '16000', '-ac', '1', '-c:a', 'pcm_s16le', w16], check=True)
        r = subprocess.run(['whisper-cli', '-m', MODEL, '-f', w16, '-l', 'en', '-nt', '-np'], capture_output=True, text=True)
        return ' '.join(r.stdout.split())

if __name__ == '__main__':
    if not os.path.exists(MODEL):
        print('no whisper model at', MODEL); sys.exit(2)
    for p in sys.argv[1:]:
        text = listen(p)
        print(heard(text), len(LYRIC), text)
