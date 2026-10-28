# Roundlot logo

The mark is a market maker's order book drawn as a depth chart. Bids sit on the left and asks on the right, three price levels a side, each deeper than the one before. The gap in the middle is the spread. It's the same book the market maker in Roundlot quotes.

| File | Use |
|------|-----|
| `roundlot-logo.svg` | Mark and name, on light backgrounds |
| `roundlot-logo-white.svg` | Mark and name, on dark backgrounds |
| `roundlot-mark.svg` | Mark alone, where the name is already nearby or space is tight |
| `roundlot-mark-white.svg` | Mark alone, on dark backgrounds |
| `roundlot-avatar.png` | 512 px square for the GitHub avatar and other profile pictures |
| `../../web/favicon.svg` | Browser tab icon; switches to white in dark mode |

## Construction

The mark sits on a 16-unit grid. Each level is 2 units wide, the levels are 3, 6 and 9 units tall, and the spread is 2 units. Every edge falls on a whole unit, so the mark stays sharp at 16, 32 and 48 pixels.

The name is set in [Archivo](https://fonts.google.com/specimen/Archivo) Bold at 112.5% width, the same family as the web UI, and converted to outlines so the files need no font. In the full logo the mark's tallest level reaches the top of the `l`, both sit on the baseline, and the gap between them is two levels wide.

## Colour

| Name | Hex | Use |
|------|-----|-----|
| Ink | `#14213d` | The logo on light backgrounds |
| White | `#ffffff` | The logo on Ink or other dark backgrounds |

The logo is always one colour. Leave at least one level's width of clear space around it, and don't use the full logo smaller than 16 px tall; use the mark alone below that.

The logo files are under the same MIT license as the code.
