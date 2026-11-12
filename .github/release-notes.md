Planish is a mixing suite for Ableton Live: eleven modules on one channel
strip (Gate, Equalizer with dynamic bands, two Compressors, Transient
Shaper, Exciter, Sculptor, Unmask, Density, Phase, Clipper), each with its
own MIX, plus an output stage with width, pan and a limiter. Every Planish
in a set knows about the others: the Equalizer's masking meter shows where
two tracks collide, and the Visual Mixer sets level, pan and width for all
of them on one pad. ASSIST listens to a track, works out what it is, and
builds a chain for it; the MIX ASSISTANT sets a starting balance for the
whole song.

## New in 0.1.1

- Fixed a crash after about six hours of running at 96 kHz (twelve at
  48 kHz). The rings that feed the meters and scopes counted samples in a
  number that eventually overflowed, and the next writes landed outside
  their buffer. They wrap safely now, however long the session stays open.

## Install

Unzip and copy `Planish.vst3` into your VST3 folder (macOS
`~/Library/Audio/Plug-Ins/VST3`, Windows `C:\Program Files\Common Files\VST3`,
Linux `~/.vst3`), then rescan plug-ins in Live. The guide (PDF) in the
download tours the panel, walks you through a first mix and covers every
module.

**macOS:** the plugin is signed and notarised by Apple, so your DAW loads it
straight away.
