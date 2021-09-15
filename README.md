# MuPDF for ToaruOS

This is an ancient (2012!) port of MuPDF to ToaruOS, recovered for ToaruOS 2.0.

Follow the instructions in the main README to obtain third-party libraries, build with `make build=release` with an active ToaruOS toolchain, and then build the app:

    cd toaru-app
    make

Everything is built statically, so expect a ~7MiB executable.
