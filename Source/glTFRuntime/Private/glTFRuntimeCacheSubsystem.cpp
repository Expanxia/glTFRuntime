// glTFRuntimeCacheSubsystem.cpp

#include "glTFRuntimeCacheSubsystem.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"

UglTFRuntimeCacheSubsystem* UglTFRuntimeCacheSubsystem::Get()
{
    if (GEngine)
    {
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            if (UWorld* World = Context.World())
            {
				if (UGameInstance* GI = World->GetGameInstance())
				{
					return GI->GetSubsystem<UglTFRuntimeCacheSubsystem>();
				}
            }
        }
    }
    return nullptr;
}

void UglTFRuntimeCacheSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("glTFRuntime Cache Subsystem Initialized"));
}

void UglTFRuntimeCacheSubsystem::Deinitialize()
{
    FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([this]()
        {
            ClearMeshCache(); 
            ClearTextureCache();
        }, TStatId(), nullptr, ENamedThreads::GameThread);
    FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);

    UE_LOG(LogTemp, Log, TEXT("glTFRuntime Cache Subsystem Deinitialized"));
    Super::Deinitialize();
}

// ---------- TEXTURE CACHE ----------
UTexture2D* UglTFRuntimeCacheSubsystem::GetCachedTexture(const FString& TextureURI) const
{
    FScopeLock Lock(&TextureCacheLock);
    if (const TWeakObjectPtr<UTexture2D>* Found = TextureCache.Find(TextureURI))
    {
        if (UTexture2D* Texture = Found->Get()) // safe .Get()
        {
            return Texture;
        }
    }
    return nullptr;
}

void UglTFRuntimeCacheSubsystem::AddCachedTexture(const FString& TextureURI, UTexture2D* Texture)
{
    if (Texture && !TextureURI.IsEmpty())
	{
		FScopeLock Lock(&TextureCacheLock);
        TextureCache.Add(TextureURI, Texture);
		UE_LOG(LogGLTFRuntime, Log, TEXT("Added texture to global cache: %s"), *TextureURI);
	}
}

void UglTFRuntimeCacheSubsystem::ClearTextureCache()
{
    FScopeLock Lock(&TextureCacheLock);
	const int32 CacheCount = TextureCache.Num();
	TextureCache.Empty();
	UE_LOG(LogGLTFRuntime, Log, TEXT("Cleared global texture cache (%d textures)"), CacheCount);
}

int32 UglTFRuntimeCacheSubsystem::GetTextureCacheSize() const
{
    FScopeLock Lock(&TextureCacheLock);
    return TextureCache.Num();
}

// ---------- MESH CACHE ----------
UStaticMesh* UglTFRuntimeCacheSubsystem::GetCachedMesh(const FString& MeshFingerprint) const
{
    FScopeLock Lock(&MeshCacheLock);
    if (const TWeakObjectPtr<UStaticMesh>* Found = MeshCache.Find(MeshFingerprint))
    {
        return Found->Get();
    }
    return nullptr;
}

void UglTFRuntimeCacheSubsystem::AddCachedMesh(const FString& MeshFingerprint, UStaticMesh* Mesh)
{
    if (Mesh && !MeshFingerprint.IsEmpty())
	{
		FScopeLock Lock(&MeshCacheLock);
        MeshCache.Add(MeshFingerprint, Mesh);
        UE_LOG(LogGLTFRuntime, Log, TEXT("Added mesh to global cache: %s"), *MeshFingerprint);
	}
}

void UglTFRuntimeCacheSubsystem::ClearMeshCache()
{
	// Clear meshes
    FScopeLock Lock(&MeshCacheLock);
    const int32 CacheCount = MeshCache.Num();
	MeshCache.Empty();
	UE_LOG(LogGLTFRuntime, Log, TEXT("Cleared global mesh cache (%d meshes)"), CacheCount);
}

int32 UglTFRuntimeCacheSubsystem::GetMeshCacheSize() const
{
    FScopeLock Lock(&MeshCacheLock);
	return MeshCache.Num();
}

// ---------- FINGERPRINT HELPERS ----------
FString UglTFRuntimeCacheSubsystem::GenerateMeshFingerprintFromBinaryData(
    const TArray<FStaticMeshBuildVertex>& StaticMeshBuildVertices,
    const TArray<uint32>& LODIndices,
    const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
    if (StaticMeshBuildVertices.Num() == 0 || LODIndices.Num() == 0)
	{
		return FString();
	}

	// Hash the actual binary mesh data
	uint32 VerticesHash = FCrc::MemCrc32(StaticMeshBuildVertices.GetData(), StaticMeshBuildVertices.Num() * sizeof(FStaticMeshBuildVertex));
	uint32 IndicesHash = FCrc::MemCrc32(LODIndices.GetData(), LODIndices.Num() * sizeof(uint32));
	
	const uint32 CombinedHash = HashCombine(VerticesHash, IndicesHash);
	
	return FString::Printf(TEXT("BinaryMesh_%u"), CombinedHash);
}

FString UglTFRuntimeCacheSubsystem::GenerateMeshFingerprint(
    TSharedPtr<FJsonObject> JsonMeshObject,
    const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
    if (!JsonMeshObject.IsValid())
	{
		return FString();
	}

	// Create a comprehensive fingerprint based on mesh data and configuration
	FString JsonString;
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(MakeShared<FJsonValueObject>(JsonMeshObject), "", JsonWriter);
	
	// Include relevant static mesh config parameters in fingerprint
	FString ConfigFingerprint = FString::Printf(TEXT("_%d_%d_%d_%d_%d_%d_%d_%d_%d_%d_%d_%s"), 
		StaticMeshConfig.bBuildSimpleCollision ? 1 : 0,
		StaticMeshConfig.bBuildComplexCollision ? 1 : 0,
		StaticMeshConfig.bReverseWinding ? 1 : 0,
		StaticMeshConfig.bAllowCPUAccess ? 1 : 0,
		StaticMeshConfig.bUseHighPrecisionUVs ? 1 : 0,
		StaticMeshConfig.bUseHighPrecisionTangentBasis ? 1 : 0,
		StaticMeshConfig.bReverseTangents ? 1 : 0,
		StaticMeshConfig.bGenerateStaticMeshDescription ? 1 : 0,
		StaticMeshConfig.bBuildNavCollision ? 1 : 0,
		StaticMeshConfig.bBuildLumenCards ? 1 : 0,
		(int32)StaticMeshConfig.PivotPosition,
		*StaticMeshConfig.CustomPivotTransform.ToString()
	);
	
	// Generate hash from JSON content and config
	const uint32 JsonHash = FCrc::StrCrc32(*JsonString);
	const uint32 ConfigHash = FCrc::StrCrc32(*ConfigFingerprint);
	const uint32 CombinedHash = HashCombine(JsonHash, ConfigHash);
	
	return FString::Printf(TEXT("Mesh_%u"), CombinedHash);
}

UStaticMesh* UglTFRuntimeCacheSubsystem::CreateTransientStaticMesh()
{
    // Create a transient UStaticMesh owned by this subsystem (safe cleanup)
    UStaticMesh* Mesh = NewObject<UStaticMesh>(this, NAME_None, RF_Transient);
    return Mesh;
}