Place SDF font files here:
  hell_verdict.png  (grayscale SDF atlas, R = distance)
  hell_verdict.json (glyph metrics, msdf-atlas-gen format)

Generate:
  msdf-atlas-gen -font YourFont.ttf -type sdf -format png \
    -imageout hell_verdict.png -json hell_verdict.json -size 48 -pxrange 4

If missing, the engine uses a built-in ASCII fallback atlas.
