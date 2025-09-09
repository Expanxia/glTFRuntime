// glTFRuntimeCacheSubsystem.h

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "glTFRuntimeCacheSubsystem.generated.h"
#include "glTFRuntimeParser.h"


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

  // Fingerprint generators
  static FString GenerateMeshFingerprintFromBinaryData(
      const TArray<FStaticMeshBuildVertex> &StaticMeshBuildVertices,
      const TArray<uint32> &LODIndices,
      const FglTFRuntimeStaticMeshConfig &StaticMeshConfig);

  static FString
  GenerateMeshFingerprint(TSharedPtr<FJsonObject> JsonMeshObject,
                          const FglTFRuntimeStaticMeshConfig &StaticMeshConfig);

private:
  mutable FCriticalSection TextureCacheLock;
  mutable FCriticalSection MeshCacheLock;

  TMap<FString, TWeakObjectPtr<UTexture2D>> TextureCache;
  TMap<FString, TWeakObjectPtr<UStaticMesh>> MeshCache;
};
