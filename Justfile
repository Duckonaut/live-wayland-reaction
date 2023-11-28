setup:
    meson setup build

build:
    meson compile -C build

run: build
    ./build/live-wayland-reaction ~/Pictures/markiplier.jpg -w 240 -m 12
