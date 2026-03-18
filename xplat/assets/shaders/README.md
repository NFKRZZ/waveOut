# Shader Assets

`waveout_xplat` loads precompiled bgfx shader binaries from:

`assets/shaders/<renderer>/<shader>.bin`

Renderer folder mapping:

- `dx11`, `dx12`
- `metal`
- `glsl`
- `essl`
- `spirv`

Required shader binaries:

- `vs_color.bin`
- `fs_color.bin`
- `vs_texture.bin`
- `fs_texture.bin`

Source `.sc` files are in `assets/shaders/src/`.
Use bgfx `shaderc` to compile them per target renderer.

