# ZYG-ZXG design direction

Identity: a musician in 2002 imagining a serious synthesizer of 2027. Use translucent blue/silver surfaces, restrained chrome/brushed plastic, compact LCD-like displays, cyan/green indicators, beveled tactile controls, and small bitmap/CRT details. Draw original components and assets. Do not copy Serum, Windows Media Player, Winamp artwork, Microsoft trade dress, or Xfer skins.

The UI should be dense and legible at common plugin sizes. Hierarchy should reveal signal flow and modulation relationships without large empty cards. Keyboard operation, precise numeric entry, accessible labels, scalable rendering and clear focus states matter as much as the visual theme.

Information architecture needs direct access to oscillators A/B/C/SUB/NOISE; sample, multisample, granular, spectral and wavetable editors; mixer/routing; filters; FX Main/Bus 1/Bus 2; matrix; envelopes/LFOs/macros; arp/clips; global/voice; and preset/asset browser. The supplied screenshot communicates information density and control grouping, not visual source assets.

Every imported setting must be inspectable and editable. Show `SERUM IMPORT` versus `ZYG NATIVE`, unresolved assets, unknown fields, and preserved-but-not-rendered state explicitly. Do not represent a parsed field as an active feature unless its native behavior exists.

The wavetable editor must eventually support drawing, frame operations, morphing/interpolation, audio import, spectral operations, generation, normalization and export. The preset browser should search/tags/author/category/favorites/recents and user folders. Both editors must operate through the independent patch model and non-real-time asset pipeline.

Current status: no GUI has been implemented. These are product design constraints, not a mockup or completed component specification.
