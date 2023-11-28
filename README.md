# live-wayland-reaction

Display an image of your choice over your Wayland compositor.

## Requirements:
- A wayland compositor supporting `wlr-layer-shell-unstable-v1`

## Usage
`live-wayland-reaction <path> [OPTIONS]`

### Options:
```
  -w, --width <width>              set the width of the overlay
                                   default: image width
  -h, --height <height>            set the height of the overlay
                                   default: image height
  -m, --margin <margin>            set the margin of the overlay
                                   default: 0
  -a, --anchor <anchor>:<anchor>   set the anchors of the overlay
                                   (top|middle|bottom):(left|middle|right)
                                   default: top:left
```

### Example:
  `live-wayland-reaction /path/to/image.png -w 240 -m 8 -a top:middle`
