#pragma once
#include "IUnityInterface.h"
#include "d3d12.h"

// Should only be used on the rendering thread unless noted otherwise.
UNITY_DECLARE_INTERFACE(IUnityGraphicsD3D12) {
  ID3D12Device*(UNITY_INTERFACE_API * GetDevice)();

  ID3D12Fence*(UNITY_INTERFACE_API * GetFrameFence)();
  // Returns the value set on the frame fence once the current frame completes or the GPU is flushed
  UINT64(UNITY_INTERFACE_API * GetNextFrameFenceValue)();

  // Returns the state of the resource in the current command list.
  bool(UNITY_INTERFACE_API * GetResourceState)(ID3D12Resource * resource,
                                                D3D12_RESOURCE_STATES * outState);
  // Perform a resource transition in the current command list, if applicable.
  void(UNITY_INTERFACE_API * SetResourceState)(ID3D12Resource * resource,
                                                D3D12_RESOURCE_STATES state);

  ID3D12CommandQueue*(UNITY_INTERFACE_API * GetCommandQueue)();

  ID3D12Resource*(UNITY_INTERFACE_API *
                  TextureFromRenderBuffer)(UnityRenderBuffer buffer);
  ID3D12Resource*(UNITY_INTERFACE_API *
                  TextureFromNativeTexture)(UnityTextureID texture);
};

UNITY_REGISTER_INTERFACE_GUID(0xEC449D8DE5B04CF1ULL,
                              0xB8C628572CCEA7DFULL,
                              IUnityGraphicsD3D12)
