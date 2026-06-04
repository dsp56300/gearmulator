# How AI is used in this project — a statement

A few people have asked us to be explicit about how we use AI in the
gearmulator project. We're happy to do that, because the reality is much
more boring (and much less worrying) than "the AI does it all".

## What AI is not

It is not the architect. It does not decide what we emulate, how the
DSP56300 emulator is structured, which bug gets fixed next, how the
voice expansion ring is wired, which parameters belong to which page,
or how a synth's firmware should be reverse-engineered. Those decisions
come from the person typing the prompt. The AI is told what to do,
not asked.

It is not the bug investigator either. When a crash report comes in,
a human reads it, forms a hypothesis based on years of working with
this code, and uses the AI to confirm or refute it. "Check the crash
dump, I assume X" is the kind of instruction the AI gets — and it
gets corrected when its conclusion disagrees with the human's domain
knowledge.

It is not the tester of the result. No AI listens to whether the
emulation sounds right, no AI checks whether a knob feels responsive
in Ableton Live, no AI verifies that a Waldorf Q boots cleanly with
the original ROM. That requires a real DAW, a real audio interface,
and human ears that know what a Q is supposed to sound like.

## What AI is

A very fast typist with a good memory for the codebase. It is used
to:

- **Type out implementations the developer has already designed.**
  "Replace this `std::function` with an atomic struct of these three
  fields, here's why" — the architectural call is human, the
  keyboard work is delegated.
- **Scan large crash dumps and stack traces** and pull out the
  relevant frames so the human doesn't have to read 6000 lines of
  Mach-O symbol table.
- **Run mechanical refactors** that are tedious but unambiguous —
  rename a field across N files, port a fix from one synth's
  device library to another, add boilerplate.
- **Cross-check assumptions** against the actual current code state
  before making a change ("does this function still exist? Is the
  member still named that?").
- **Draft commit messages and ticket comments** that the developer
  then reads and adjusts before sending.

It runs unit tests. It runs builds. It does not push commits without
explicit approval, and every commit message is reviewed by the
developer before it lands.

## What that looks like in practice

A representative session, paraphrased:

> **Dev:** The voice expansion is causing CPU spikes for Vavra. The
> single DSP mode is fine. I think the buffer priming is wrong, but
> Xenia and Vavra route ESAI differently — Xenia has separate paths,
> Vavra shares one. Apply the priming to the right places only.
>
> **AI:** [proposes patch covering all three places]
>
> **Dev:** Add 1 and 3, not 2. Reason: Xenia uses a different ESSI
> for the chain routing than for ADC/DAC, but for Vavra there is
> only one ESAI doing everything.
>
> **AI:** [adjusts]
>
> **Dev:** I removed the priming in mqdsp.cpp myself, because in VE
> mode the audio is drained after the initial magic init, so it
> wouldn't have helped anyway. Tell me if I'm wrong.

The technical insight is the developer's. The AI is the second pair
of hands.

When the AI gets something wrong — and it does, regularly — it gets
told. "Three independent atomics is not atomic, can we encapsulate
this in a single one?" "You compiled in `temp/cmake_linux`, but on
that machine you should be using `temp/cmake_linux_devpi5` —
remember that for next time." "Why are you adding so much code?"
Those corrections are how the work stays honest.

## The verification model

Every change goes through the same loop it always has:

1. The developer decides what should change and why.
2. The change is implemented (often using AI for the typing).
3. It is built locally.
4. It is run inside a real DAW with real hardware ROMs.
5. It is checked against the actual reported bug, not against
   "looks right".
6. Only then does it get committed, pushed, and shipped.

Steps 1, 4, 5, and 6 are entirely human. The AI assists steps 2 and 3.

## Why we use it at all

Because the project is large, the developer count is small, and a
faster typing assistant means more bugs fixed, more synths emulated,
and more of the developer's time spent on the parts that actually
require the developer — DSP reverse engineering, hardware comparison,
audio fidelity, listening tests.

The alternative isn't "no AI, same output." The alternative is "no
AI, less output." The choice was easy.

## On the irony

Yes, we noticed: a statement about how we don't let the AI run the
project was, in fact, drafted by the AI on the developer's
instruction. That is exactly the relationship we are describing.
The wording was prompted, reviewed, edited, and approved by a
human. The keystrokes were not.
