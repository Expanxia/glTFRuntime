// glTFRuntimeCacheSubsystem.h

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "glTFRuntimeParser.h"
#include "glTFRuntimeCacheSubsystem.generated.h"


struct FPendingDownload
{
    FString Uri;
    FString CacheFilename;
    bool bSuccess = false;
};

struct FPendingDownloadTask
{
    FString Url;
    FString CacheFilename;
    TArray<TFunction<void(const FPendingDownload&)>> Callbacks;
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> HttpRequest;
};

UCLASS()
class GLTFRUNTIME_API UglTFRuntimeCacheSubsystem
    : public UGameInstanceSubsystem {
  GENERATED_BODY()

public:
  static UglTFRuntimeCacheSubsystem *Get();

  // Called when subsystem is initialized
  virtual void Initialize(FSubsystemCollectionBase &Collection) override;

  // Called when subsystem is deinitialized (PIE stop / game shutdown)
  virtual void Deinitialize() override;

  // Texture cache
  UTexture2D *GetCachedTexture(const FString &TextureURI) const;
  void AddCachedTexture(const FString &TextureURI, UTexture2D *Texture);
  void ClearTextureCache();
  int32 GetTextureCacheSize() const;

  // Mesh cache
  UStaticMesh *GetCachedMesh(const FString &MeshFingerprint) const;
  void AddCachedMesh(const FString &MeshFingerprint, UStaticMesh *Mesh);
  void ClearMeshCache();
  int32 GetMeshCacheSize() const;

  void DownloadExternalFiles(
      const FString& BaseUrl,
      const FString& CachePath,
      const TArray<FString>& Uris,
      TFunction<void(const TArray<FPendingDownload>&)> OnAllComplete,
      bool bUseCacheOnError);

  // Fingerprint generators
  static FString GenerateMeshFingerprintFromBinaryData(
      const TArray<FStaticMeshBuildVertex> &StaticMeshBuildVertices,
      const TArray<uint32> &LODIndices,
      const FglTFRuntimeStaticMeshConfig &StaticMeshConfig);

  static FString
  GenerateMeshFingerprint(TSharedPtr<FJsonObject> JsonMeshObject,
                          const FglTFRuntimeStaticMeshConfig &StaticMeshConfig);

  static TArray<FString> GetExternalUris(const TSharedPtr<FJsonObject>& GltfJson);

  UPROPERTY()
  int32 MaxConcurrentRequests = 10;

private:
  void PumpQueue(bool bUseCacheOnError);
  void StartRequest(FPendingDownloadTask& Task, bool bUseCacheOnError);

  mutable FCriticalSection TextureCacheLock;
  mutable FCriticalSection MeshCacheLock;

  TMap<FString, TWeakObjectPtr<UTexture2D>> TextureCache;
  TMap<FString, TWeakObjectPtr<UStaticMesh>> MeshCache;

  TMap<FString, FPendingDownloadTask> ActiveDownloads; // key = CacheFilename
  TQueue<FString, EQueueMode::Mpsc> PendingQueue;
  TSet<FString> PendingSet;

  int32 CurrentActiveRequests = 0;
};
