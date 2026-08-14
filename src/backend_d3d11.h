#pragma once
// Installs the native backend: hooks Direct3DCreate9 in the host exe and returns
// an IDirect3DDevice9 backed by real D3D11 (not a shim). See ARCHITECTURE.md.
void InstallNativeD3D9();
