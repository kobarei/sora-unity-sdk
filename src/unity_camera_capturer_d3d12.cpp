#include "unity_camera_capturer.h"

namespace sora_unity_sdk {

UnityCameraCapturer::D3D12Impl::~D3D12Impl() {
  if (fence_event_ != NULL) {
    CloseHandle(fence_event_);
    fence_event_ = NULL;
  }
  if (readback_fence_ != nullptr) {
    ((ID3D12Fence*)readback_fence_)->Release();
    readback_fence_ = nullptr;
  }
  if (readback_buffer_ != nullptr) {
    ((ID3D12Resource*)readback_buffer_)->Release();
    readback_buffer_ = nullptr;
  }
}

bool UnityCameraCapturer::D3D12Impl::Init(UnityContext* context,
                                          void* camera_texture,
                                          int width,
                                          int height) {
  context_ = context;
  camera_texture_ = camera_texture;
  width_ = width;
  height_ = height;
  fence_value_ = 0;

  auto device = context->GetDeviceD3D12();
  if (device == nullptr) {
    return false;
  }

  // カメラテクスチャのリソース記述を取得してサイズを計算
  ID3D12Resource* camera_resource = (ID3D12Resource*)camera_texture;
  D3D12_RESOURCE_DESC texture_desc = camera_resource->GetDesc();

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
  UINT num_rows;
  UINT64 row_size_in_bytes;
  UINT64 total_bytes;
  device->GetCopyableFootprints(
      &texture_desc, 0, 1, 0, &layout, &num_rows, &row_size_in_bytes, &total_bytes);

  // リードバック用のバッファを作成
  D3D12_HEAP_PROPERTIES heap_props = {};
  heap_props.Type = D3D12_HEAP_TYPE_READBACK;
  heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  heap_props.CreationNodeMask = 1;
  heap_props.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC resource_desc = {};
  resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  resource_desc.Alignment = 0;
  resource_desc.Width = total_bytes;
  resource_desc.Height = 1;
  resource_desc.DepthOrArraySize = 1;
  resource_desc.MipLevels = 1;
  resource_desc.Format = DXGI_FORMAT_UNKNOWN;
  resource_desc.SampleDesc.Count = 1;
  resource_desc.SampleDesc.Quality = 0;
  resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

  ID3D12Resource* readback_buffer = nullptr;
  HRESULT hr = device->CreateCommittedResource(
      &heap_props,
      D3D12_HEAP_FLAG_NONE,
      &resource_desc,
      D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr,
      IID_PPV_ARGS(&readback_buffer));

  if (!SUCCEEDED(hr)) {
    RTC_LOG(LS_ERROR) << "ID3D12Device::CreateCommittedResource is failed: hr=" << hr;
    return false;
  }

  readback_buffer_ = readback_buffer;

  // Fenceを作成
  ID3D12Fence* fence = nullptr;
  hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
  if (!SUCCEEDED(hr)) {
    RTC_LOG(LS_ERROR) << "ID3D12Device::CreateFence is failed: hr=" << hr;
    return false;
  }
  readback_fence_ = fence;

  // Fenceイベントを作成
  fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (fence_event_ == NULL) {
    RTC_LOG(LS_ERROR) << "CreateEvent is failed";
    return false;
  }

  return true;
}

webrtc::scoped_refptr<webrtc::I420Buffer>
UnityCameraCapturer::D3D12Impl::Capture() {
  auto device = context_->GetDeviceD3D12();
  auto command_queue = context_->GetCommandQueueD3D12();
  
  if (device == nullptr || command_queue == nullptr) {
    RTC_LOG(LS_ERROR) << "ID3D12Device or ID3D12CommandQueue is null";
    return nullptr;
  }

  // コマンドアロケータとコマンドリストを作成
  ID3D12CommandAllocator* command_allocator = nullptr;
  HRESULT hr = device->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator));
  if (!SUCCEEDED(hr)) {
    RTC_LOG(LS_ERROR) << "ID3D12Device::CreateCommandAllocator is failed: hr=" << hr;
    return nullptr;
  }

  ID3D12GraphicsCommandList* command_list = nullptr;
  hr = device->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator, nullptr,
      IID_PPV_ARGS(&command_list));
  if (!SUCCEEDED(hr)) {
    RTC_LOG(LS_ERROR) << "ID3D12Device::CreateCommandList is failed: hr=" << hr;
    command_allocator->Release();
    return nullptr;
  }

  ID3D12Resource* camera_resource = (ID3D12Resource*)camera_texture_;

  // テクスチャの説明を取得
  D3D12_RESOURCE_DESC texture_desc = camera_resource->GetDesc();

  // Unityのインターフェースを使ってリソースステートを取得・変更
  auto unity_d3d12 = context_->GetInterfaces()->Get<IUnityGraphicsD3D12>();
  D3D12_RESOURCE_STATES current_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
  bool state_retrieved = false;
  
  if (unity_d3d12 != nullptr) {
    state_retrieved = unity_d3d12->GetResourceState(camera_resource, &current_state);
    if (state_retrieved) {
      // UnityにCOPY_SOURCEステートへの遷移を要求
      unity_d3d12->SetResourceState(camera_resource, D3D12_RESOURCE_STATE_COPY_SOURCE);
    }
  }

  // リソースバリア: テクスチャをCOPY_SOURCEステートに遷移
  if (!state_retrieved || current_state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = camera_resource;
    barrier.Transition.StateBefore = current_state;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list->ResourceBarrier(1, &barrier);
  }

  // テクスチャからリードバックバッファにコピー
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
  UINT num_rows;
  UINT64 row_size_in_bytes;
  UINT64 total_bytes;
  device->GetCopyableFootprints(
      &texture_desc, 0, 1, 0, &layout, &num_rows, &row_size_in_bytes, &total_bytes);

  D3D12_TEXTURE_COPY_LOCATION src = {};
  src.pResource = camera_resource;
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION dst = {};
  dst.pResource = (ID3D12Resource*)readback_buffer_;
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint = layout;

  command_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

  // リソースバリア: テクスチャを元のステートに戻す
  if (!state_retrieved || current_state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = camera_resource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = current_state;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list->ResourceBarrier(1, &barrier);
  }

  // Unityにリソースステートを元に戻すことを通知
  if (state_retrieved && unity_d3d12 != nullptr) {
    unity_d3d12->SetResourceState(camera_resource, current_state);
  }

  // コマンドリストをクローズ
  hr = command_list->Close();
  if (!SUCCEEDED(hr)) {
    RTC_LOG(LS_ERROR) << "ID3D12GraphicsCommandList::Close is failed: hr=" << hr;
    command_list->Release();
    command_allocator->Release();
    return nullptr;
  }

  // コマンドリストを実行
  ID3D12CommandList* command_lists[] = { command_list };
  command_queue->ExecuteCommandLists(1, command_lists);

  // Fenceでコマンド完了を待機
  fence_value_++;
  hr = command_queue->Signal((ID3D12Fence*)readback_fence_, fence_value_);
  if (!SUCCEEDED(hr)) {
    RTC_LOG(LS_ERROR) << "ID3D12CommandQueue::Signal is failed: hr=" << hr;
    command_list->Release();
    command_allocator->Release();
    return nullptr;
  }

  if (((ID3D12Fence*)readback_fence_)->GetCompletedValue() < fence_value_) {
    hr = ((ID3D12Fence*)readback_fence_)->SetEventOnCompletion(fence_value_, fence_event_);
    if (!SUCCEEDED(hr)) {
      RTC_LOG(LS_ERROR) << "ID3D12Fence::SetEventOnCompletion is failed: hr=" << hr;
      command_list->Release();
      command_allocator->Release();
      return nullptr;
    }
    WaitForSingleObject(fence_event_, INFINITE);
  }

  // リードバックバッファからデータを読み取る
  void* mapped_data = nullptr;
  D3D12_RANGE read_range = { 0, static_cast<SIZE_T>(total_bytes) };
  hr = ((ID3D12Resource*)readback_buffer_)->Map(0, &read_range, &mapped_data);
  if (!SUCCEEDED(hr)) {
    RTC_LOG(LS_ERROR) << "ID3D12Resource::Map is failed: hr=" << hr;
    command_list->Release();
    command_allocator->Release();
    return nullptr;
  }

  // Windows の場合は座標系の関係で上下反転してるので、頑張って元の向きに戻す
  std::unique_ptr<uint8_t[]> buf(new uint8_t[width_ * height_ * 4]);
  for (int i = 0; i < height_; i++) {
    std::memcpy(buf.get() + width_ * 4 * i,
                ((const uint8_t*)mapped_data) + layout.Footprint.RowPitch * (height_ - i - 1),
                width_ * 4);
  }

  // I420 に変換
  webrtc::scoped_refptr<webrtc::I420Buffer> i420_buffer =
      webrtc::I420Buffer::Create(width_, height_);
  libyuv::ARGBToI420(buf.get(), width_ * 4,
                     i420_buffer->MutableDataY(), i420_buffer->StrideY(),
                     i420_buffer->MutableDataU(), i420_buffer->StrideU(),
                     i420_buffer->MutableDataV(), i420_buffer->StrideV(),
                     width_, height_);

  ((ID3D12Resource*)readback_buffer_)->Unmap(0, nullptr);

  // クリーンアップ
  command_list->Release();
  command_allocator->Release();

  return i420_buffer;
}

}  // namespace sora_unity_sdk
