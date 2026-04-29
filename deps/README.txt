Header-only deps (not included — download before building):

1. EnTT:
   git clone --depth 1 https://github.com/skypjack/entt deps/entt

2. stb_image:
   git clone --depth 1 https://github.com/nothings/stb deps/stb
   mkdir deps/stb/stb && cp deps/stb/stb_image.h deps/stb/stb/

3. GLAD (optional OpenGL fallback):
   Generate at https://glad.dav1d.de/ (OpenGL 4.1 Core Profile)
   Place in deps/glad/

GLM + SDL2 = system packages (libglm-dev, libsdl2-dev)
