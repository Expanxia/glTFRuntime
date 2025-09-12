// glTFRuntimeCacheSubsystem.cpp

#include "glTFRuntimeCacheSubsystem.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"

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

void UglTFRuntimeCacheSubsystem::DownloadExternalFiles(
    const FString& BaseUrl,
    const FString& CachePath,
    const TArray<FString>& Uris,
    TFunction<void(const TArray<FPendingDownload>&)> OnAllComplete,
    bool bUseCacheOnError)
{
    TSharedRef<TArray<FPendingDownload>> Pending = MakeShared<TArray<FPendingDownload>>();
    Pending->Reserve(Uris.Num());

    TSharedRef<int32> Remaining = MakeShared<int32>(Uris.Num());

    for (const FString& Uri : Uris)
    {
        FString CacheFilename = CachePath / FPaths::GetCleanFilename(Uri);
        FString FullUrl = FPaths::Combine(BaseUrl, Uri);

        // If an active download exists, attach callback and continue
        if (ActiveDownloads.Contains(CacheFilename))
        {
            ActiveDownloads[CacheFilename].Callbacks.Add(
                [Pending, Remaining, OnAllComplete](const FPendingDownload& Result)
                {
                    Pending->Add(Result);
                    (*Remaining)--;
                    if (*Remaining == 0)
                    {
                        OnAllComplete(*Pending);
                    }
                });
            continue;
        }

        // If already queued, attach callback to the queued entry in ActiveDownloads (we create the entry now)
        if (PendingSet.Contains(CacheFilename))
        {
            // ActiveDownloads should contain an entry even for queued items � ensure it does
            if (ActiveDownloads.Contains(CacheFilename))
            {
                ActiveDownloads[CacheFilename].Callbacks.Add(
                    [Pending, Remaining, OnAllComplete](const FPendingDownload& Result)
                    {
                        Pending->Add(Result);
                        (*Remaining)--;
                        if (*Remaining == 0)
                        {
                            OnAllComplete(*Pending);
                        }
                    });
                continue;
            }
            else
            {
                // This should not normally happen, but log to help debug
                UE_LOG(LogTemp, Warning, TEXT("PendingSet contains %s but ActiveDownloads has no entry"), *CacheFilename);
            }
        }

        // Create an ActiveDownloads entry immediately (prepares callback list). This prevents duplicates from being queued.
        FPendingDownloadTask NewTask;
        NewTask.Url = FullUrl;
        NewTask.CacheFilename = CacheFilename;
        NewTask.Callbacks.Add(
            [Pending, Remaining, OnAllComplete](const FPendingDownload& Result)
            {
                Pending->Add(Result);
                (*Remaining)--;
                if (*Remaining == 0)
                {
                    OnAllComplete(*Pending);
                }
            });

        ActiveDownloads.Add(CacheFilename, MoveTemp(NewTask));

        // Enqueue key and mark as pending
        PendingQueue.Enqueue(CacheFilename);
        PendingSet.Add(CacheFilename);
    }

    // Try to pump the queue
    PumpQueue(bUseCacheOnError);
}

void UglTFRuntimeCacheSubsystem::PumpQueue(bool bUseCacheOnError)
{
    // Debug log
    UE_LOG(LogTemp, Verbose, TEXT("PumpQueue called. Active=%d, Pending=%d, CurrentActive=%d"),
        ActiveDownloads.Num(), PendingSet.Num(), CurrentActiveRequests);

    // Start as many as allowed
    while (CurrentActiveRequests < MaxConcurrentRequests)
    {
        FString CacheFilename;
        if (!PendingQueue.Dequeue(CacheFilename))
        {
            break;
        }

        // Remove from pending set because it's being started now
        PendingSet.Remove(CacheFilename);

        // Ensure the ActiveDownloads entry exists
        FPendingDownloadTask* TaskPtr = ActiveDownloads.Find(CacheFilename);
        if (!TaskPtr)
        {
            UE_LOG(LogTemp, Warning, TEXT("PumpQueue: no ActiveDownloads entry for %s"), *CacheFilename);
            continue;
        }

        // Start the request using the map-owned task (so we don't operate on temporaries)
        StartRequest(*TaskPtr, bUseCacheOnError);
    }
}

void UglTFRuntimeCacheSubsystem::StartRequest(FPendingDownloadTask& Task, bool bUseCacheOnError)
{
    // guard
    if (Task.HttpRequest.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("StartRequest called but HttpRequest already exists for %s"), *Task.CacheFilename);
        return;
    }

    CurrentActiveRequests++;

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Task.HttpRequest = Request; // store into the map-held task

    Request->SetVerb(TEXT("GET"));
    Request->SetURL(Task.Url);
    UE_LOG(LogTemp, Warning, TEXT("REQUESTING: %s -> %s"), *Task.Url, *Task.CacheFilename);

    if (FPaths::FileExists(Task.CacheFilename))
    {
        const FDateTime CacheModificationTime = IFileManager::Get().GetTimeStamp(*Task.CacheFilename);
        Request->AppendToHeader(TEXT("If-Modified-Since"), CacheModificationTime.ToHttpDate());
    }

    // capture CacheFilename by value to find the map entry on completion
    Request->OnProcessRequestComplete().BindLambda(
        [this, CacheFilename = Task.CacheFilename, bUseCacheOnError]
        (FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSuccess)
        {
            // We�re on the game thread here. Copy out safe data.
            const bool bValidResp = (bSuccess && Resp.IsValid());
            const int32 RespCode = bValidResp ? Resp->GetResponseCode() : -1;
            const TArray<uint8> ResponseData = bValidResp ? Resp->GetContent() : TArray<uint8>();
            const FString LastModified = bValidResp ? Resp->GetHeader(TEXT("Last-Modified")) : FString();
            const bool bCacheValid = FPaths::FileExists(CacheFilename);

            // Do heavy work async
            Async(EAsyncExecution::ThreadPool, [this, CacheFilename, ResponseData, LastModified, RespCode, bValidResp, bCacheValid, bUseCacheOnError]()
                {
                    FPendingDownload Result;
                    Result.Uri = CacheFilename;
                    Result.CacheFilename = CacheFilename;
                    Result.bSuccess = false;

                    if (bValidResp)
                    {
                        if (RespCode == 200)
                        {
                            // Save file
                            IFileManager::Get().MakeDirectory(*FPaths::GetPath(CacheFilename), true);
                            if (FFileHelper::SaveArrayToFile(ResponseData, *CacheFilename))
                            {
                                Result.bSuccess = true;

                                // Set timestamp if server provided one
                                FDateTime ServerTime;
                                if (FDateTime::ParseHttpDate(LastModified, ServerTime))
                                {
                                    FPlatformFileManager::Get().GetPlatformFile().SetTimeStamp(*CacheFilename, ServerTime);
                                }
                                UE_LOG(LogTemp, Warning, TEXT("WRITING CACHE: %s"), *CacheFilename);
                            }
                        }
                        else if (RespCode == 304 && bCacheValid)
                        {
                            Result.bSuccess = true;
                            UE_LOG(LogTemp, Warning, TEXT("Loading %s from cache (304)"), *CacheFilename);
                        }
                    }
                    else if (bCacheValid && bUseCacheOnError)
                    {
                        Result.bSuccess = true;
                        UE_LOG(LogTemp, Warning, TEXT("Network error, falling back to cache: %s"), *CacheFilename);
                    }

                    // Bounce back to game thread for callbacks + state cleanup
                    AsyncTask(ENamedThreads::GameThread, [this, CacheFilename, Result, bUseCacheOnError]()
                        {
                            if (FPendingDownloadTask* FinishedTask = ActiveDownloads.Find(CacheFilename))
                            {
                                // Copy callbacks before removal
                                TArray<TFunction<void(const FPendingDownload&)>> Callbacks = FinishedTask->Callbacks;

                                // Remove before calling
                                ActiveDownloads.Remove(CacheFilename);
                                CurrentActiveRequests = FMath::Max(0, CurrentActiveRequests - 1);

                                for (auto& Callback : Callbacks)
                                {
                                    Callback(Result);
                                }
                            }
                            else
                            {
                                CurrentActiveRequests = FMath::Max(0, CurrentActiveRequests - 1);
                                UE_LOG(LogTemp, Warning, TEXT("StartRequest completion: no ActiveDownloads entry for %s"), *CacheFilename);
                            }

                            // Pump next queued requests
                            PumpQueue(bUseCacheOnError);
                        });
                });
        });


    Request->ProcessRequest();
}

TArray<FString> UglTFRuntimeCacheSubsystem::GetExternalUris(const TSharedPtr<FJsonObject>& GltfJson)
{
	TArray<FString> ExternalUris;

	// Buffers
	const TArray<TSharedPtr<FJsonValue>>* Buffers;
	if (GltfJson->TryGetArrayField(TEXT("buffers"), Buffers))
	{
		for (const TSharedPtr<FJsonValue>& BufferVal : *Buffers)
		{
			const TSharedPtr<FJsonObject>* BufferObj;
			if (BufferVal->TryGetObject(BufferObj))
			{
				FString Uri;
				if ((*BufferObj)->TryGetStringField(TEXT("uri"), Uri))
				{
					ExternalUris.Add(Uri);
				}
			}
		}
	}

	// Images
	const TArray<TSharedPtr<FJsonValue>>* Images;
	if (GltfJson->TryGetArrayField(TEXT("images"), Images))
	{
		for (const TSharedPtr<FJsonValue>& ImageVal : *Images)
		{
			const TSharedPtr<FJsonObject>* ImageObj;
			if (ImageVal->TryGetObject(ImageObj))
			{
				FString Uri;
				if ((*ImageObj)->TryGetStringField(TEXT("uri"), Uri))
				{
					ExternalUris.Add(Uri);
				}
			}
		}
	}

	return ExternalUris;
}