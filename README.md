# ps5-deps

Copies of the PS5 build dependencies used by `ps5/build.sh`, kept here because the original
repositories (blackbearreloaded/ps5-native-app-boilerplate and ps5-opengl) are no longer public.

- `boilerplate.tar.gz`: ps5-native-app-boilerplate at commit b8dc4a3 with its payload SDK
  (GPL-3.0-or-later).
- `glue.tar.gz`: native-app integration files from ps5-opengl at commit db752da (MIT).
- `ps5-opengl-sdk-1.0.1.tar.gz.part*`: the prebuilt ps5-opengl SDK 1.0.1 release archive, split
  into parts under 100 MB (concatenate them; the build script checks the archive's SHA-256).

Licenses are those of the original projects.
