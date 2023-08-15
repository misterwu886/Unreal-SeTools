﻿#include "C2MStaticMesh.h"
#include "C2MMaterialInstance.h"
#include "EditorModeManager.h"
#include "ObjectTools.h"
#include "StaticMeshAttributes.h"
#include "IMeshBuilderModule.h"
#include "Utils/C2MUtilsImport.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "StaticMeshOperations.h"
#include "Rendering/SkeletalMeshModel.h"
#include "EditorFramework/AssetImportData.h"

/* Create FRawMesh from C2Mesh
 * UNUSED
 */

/* Create FMeshDescription from C2Mesh
 * 
 */
FMeshDescription C2MStaticMesh::CreateMeshDescription(C2Mesh* InMesh)
{
    // Prepare our base Mesh Description
    FMeshDescription MeshDescription;
    FStaticMeshAttributes CombinedMeshAttributes{ MeshDescription };
    CombinedMeshAttributes.Register();
    CombinedMeshAttributes.RegisterPolygonNormalAndTangentAttributes();

    const TVertexAttributesRef<FVector> TargetVertexPositions = CombinedMeshAttributes.GetVertexPositions();
    const TPolygonGroupAttributesRef<FName> PolygonGroupNames = CombinedMeshAttributes.GetPolygonGroupMaterialSlotNames();
    const TVertexInstanceAttributesRef<FVector> TargetVertexInstanceNormals = CombinedMeshAttributes.GetVertexInstanceNormals();
    const TVertexInstanceAttributesRef<FVector2D> TargetVertexInstanceUVs = CombinedMeshAttributes.GetVertexInstanceUVs();
    const TVertexInstanceAttributesRef<FVector4> TargetVertexInstanceColors = CombinedMeshAttributes.GetVertexInstanceColors();

    const int32 VertexCount = InMesh->Header->BoneCountBuffer;
    TargetVertexInstanceUVs.SetNumIndices(InMesh->UVSetCount + 1); // We add 1 slot for Lightmap UV
    MeshDescription.ReserveNewVertices(InMesh->Header->BoneCountBuffer);
    MeshDescription.ReserveNewVertexInstances(InMesh->Header->BoneCountBuffer);
    MeshDescription.ReserveNewPolygons(InMesh->Header->MaterialCountBuffer);
    MeshDescription.ReserveNewEdges(InMesh->Header->MaterialCountBuffer * 2);
    TArray<FVertexID> VertexIndexToVertexID;
    VertexIndexToVertexID.Reserve(VertexCount);
    TArray<FVertexInstanceID> VertexIndexToVertexInstanceID;
    VertexIndexToVertexInstanceID.Reserve(VertexCount);
    int32 GlobalVertexIndex = 0;
	TArray<FVector> Verts;

    // Create vertices and vertex instances
    for (auto surfirst : InMesh->Surfaces)
    {
        for (int i = 0; i < surfirst->Vertexes.Num(); i++)
        {
            const FVertexID VertexID = MeshDescription.CreateVertex();
            TargetVertexPositions[VertexID] = surfirst->Vertexes[i].Vertice;
            VertexIndexToVertexID.Add(VertexID);

            const FVertexInstanceID VertexInstanceID = MeshDescription.CreateVertexInstance(VertexID);
            VertexIndexToVertexInstanceID.Add(VertexInstanceID);
            GlobalVertexIndex++;

            TargetVertexInstanceNormals[VertexInstanceID] = surfirst->Vertexes[i].Normal;
            TargetVertexInstanceColors[VertexInstanceID] = surfirst->Vertexes[i].Color.ToVector();

            //TArray<FVector2D> VertexUVs = InMesh->UVs[i];
            for (int u = 0; u < InMesh->UVSetCount; u++)
            {
				//TargetVertexInstanceUVs.Set(VertexInstanceID, u, FVector2D(surfirst->Vertexes[i].UV.X, 1 - surfirst->Vertexes[i].UV.Y));
                TargetVertexInstanceUVs.Set(VertexInstanceID, u, FVector2D(surfirst->Vertexes[i].UV.X, surfirst->Vertexes[i].UV.Y)); // We gotta flip UV
            }
        }
    }

    // Create polygons and assign polygon group names
    for (auto Surface : InMesh->Surfaces)
    {
        const FPolygonGroupID PolygonGroup = MeshDescription.CreatePolygonGroup();

        for (int f = 0; f < Surface->Faces.Num(); f++)
        {
            auto Face = Surface->Faces[f];

            // Check for degenerate face (where two or more indices are the same)
            if (Face.index[0] == Face.index[1] ||
                Face.index[1] == Face.index[2] ||
                Face.index[2] == Face.index[0])
            {
                // Skip processing degenerate faces
                continue;
            }

            TArray<FVertexInstanceID> VertexInstanceIDs;
            VertexInstanceIDs.SetNum(3);
			// Rearrange vertex indices to match Blender order


            // Grab Vertex Instances for the current face
            for (int i = 0; i < 3; i++)
            {
				const uint32_t VertexIndex = Face.index[i];
                VertexInstanceIDs[i] = VertexIndexToVertexInstanceID[VertexIndex];

            }


            MeshDescription.CreatePolygon(PolygonGroup, VertexInstanceIDs);
        }

        PolygonGroupNames[PolygonGroup] = FName(Surface->Name);
    }

    return MeshDescription;
}


UObject* C2MStaticMesh::CreateStaticMeshFromMeshDescription(UObject* ParentPackage,FMeshDescription& inMeshDescription, C2Mesh* InMesh,  TArray<C2Material*> CoDMaterials)
{
	FString ObjectName = InMesh->Header->MeshName.Replace(TEXT("::"), TEXT("_"));
	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(ParentPackage, FName(*ObjectName), RF_Public | RF_Standalone);
	// Set default settings
	StaticMesh->InitResources();
	StaticMesh->SetLightingGuid();
	// StaticMesh->bGenerateMeshDistanceField = false;
	FStaticMeshSourceModel& SrcModel = StaticMesh->AddSourceModel();
	SrcModel.BuildSettings.bRecomputeNormals = false;
	SrcModel.BuildSettings.bRecomputeTangents = true;
	SrcModel.BuildSettings.bRemoveDegenerates = false;
	SrcModel.BuildSettings.bUseHighPrecisionTangentBasis = false;
	SrcModel.BuildSettings.bUseFullPrecisionUVs = false;
	SrcModel.BuildSettings.bGenerateLightmapUVs = true;
	SrcModel.BuildSettings.SrcLightmapIndex = 0;
	SrcModel.BuildSettings.DstLightmapIndex = InMesh->UVSetCount; // We use last UV set for lightmap
	SrcModel.BuildSettings.bUseMikkTSpace = true;

	// Get & Set Mesh Description
	FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(0);
	
	if (!MeshDescription)
	{
		MeshDescription = StaticMesh->CreateMeshDescription(0);
	}
	*MeshDescription = inMeshDescription;
	StaticMesh->CommitMeshDescription(0);

	// Create materials and mesh sections
	for (int i = 0; i < InMesh->Surfaces.Num(); i++)
	{
		const auto Surface = InMesh->Surfaces[i];
		
		// Static Material for Surface
		FStaticMaterial&& UEMat = FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface));
		if(Surface->Materials.Num() > 0)
		{
			// Create an array of Surface Materials
			TArray<C2Material*> SurfMaterials;
			SurfMaterials.Reserve(Surface->Materials.Num());
			for (const uint16_t MaterialIndex : Surface->Materials)
				SurfMaterials.Push(CoDMaterials[MaterialIndex]);
			UMaterialInterface* MaterialInstance = C2MMaterialInstance::CreateMixMaterialInstance( SurfMaterials,ParentPackage);
			UEMat = FStaticMaterial(MaterialInstance);
		}
		UEMat.UVChannelData.bInitialized = true;
		UEMat.MaterialSlotName = FName(Surface->Name);
		UEMat.ImportedMaterialSlotName = FName(Surface->Name);
		StaticMesh->StaticMaterials.Add(UEMat);
		//StaticMesh->GetStaticMaterials().Add(UEMat);
		StaticMesh->GetSectionInfoMap().Set(0, i, FMeshSectionInfo(i));
	}
	StaticMesh->GetOriginalSectionInfoMap().CopyFrom(StaticMesh->GetSectionInfoMap());
	StaticMesh->ImportVersion = EImportStaticMeshVersion::LastVersion;
	// Editor builds cache the mesh description so that it can be preserved during map reloads etc
	
	TArray<FText> BuildErrors;
	// Build mesh from source
	StaticMesh->Build(false);
	StaticMesh->EnforceLightmapRestrictions();
	// StaticMesh->PostEditChange();
	StaticMesh->MarkPackageDirty();
	

	// Notify asset registry of new asset
	FAssetRegistryModule::AssetCreated(StaticMesh);
	ThumbnailTools::GenerateThumbnailForObjectToSaveToDisk(StaticMesh);
	UE_LOG(LogTemp, Log, TEXT("Static Mesh created: %s"), *ObjectName);
	//
	return StaticMesh;
}

UObject* C2MStaticMesh::CreateStaticMesh(UObject* ParentPackage, C2Mesh* InMesh,  TArray<C2Material*> CoDMaterials)
{
	UObject* MeshUsed = nullptr;
	FMeshDescription InMeshDescription = CreateMeshDescription(InMesh);
	MeshUsed = CreateStaticMeshFromMeshDescription(ParentPackage,InMeshDescription, InMesh, CoDMaterials);
	return MeshUsed;
    	
}

UObject* C2MStaticMesh::CreateSkeletalMesh(UObject* ParentPackage, C2Mesh* InMesh,  TArray<C2Material*> CoDMaterials)
{
	UObject* MeshUsed = nullptr;
	FMeshDescription InMeshDescription = CreateMeshDescription(InMesh);
	MeshUsed = CreateSkeletalMeshFromMeshDescription(ParentPackage,InMeshDescription,InMesh,CoDMaterials);
	return MeshUsed;
}


void C2MStaticMesh::ProcessSkeleton(const FSkeletalMeshImportData& ImportData, const USkeleton* Skeleton, FReferenceSkeleton& OutRefSkeleton, int& OutSkeletalDepth)
{
	const auto RefBonesBinary = ImportData.RefBonesBinary;
	OutRefSkeleton.Empty();

	FReferenceSkeletonModifier RefSkeletonModifier(OutRefSkeleton, Skeleton);

	for (const auto Bone : RefBonesBinary)
	{
		const FMeshBoneInfo BoneInfo(FName(*Bone.Name), Bone.Name, Bone.ParentIndex);
		RefSkeletonModifier.Add(BoneInfo, FTransform(Bone.BonePos.Transform));
	}

	OutSkeletalDepth = 0;

	TArray<int> SkeletalDepths;
	SkeletalDepths.Empty(ImportData.RefBonesBinary.Num());
	SkeletalDepths.AddZeroed(ImportData.RefBonesBinary.Num());
	for (auto b = 0; b < OutRefSkeleton.GetNum(); b++)
	{
		const auto Parent = OutRefSkeleton.GetParentIndex(b);
		auto Depth = 1.0f;

		SkeletalDepths[b] = 1.0f;
		if (Parent != INDEX_NONE)
		{
			Depth += SkeletalDepths[Parent];
		}
		if (OutSkeletalDepth < Depth)
		{
			OutSkeletalDepth = Depth;
		}
		SkeletalDepths[b] = Depth;
	}
}



UObject* C2MStaticMesh::CreateSkeletalMeshFromMeshDescription(UObject* ParentPackage,FMeshDescription& InMeshDescription, C2Mesh* InMesh,  TArray<C2Material*> CoDMaterials)
{
	FString ObjectName = InMesh->Header->MeshName.Replace(TEXT("::"), TEXT("_"));
	
	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(ParentPackage, FName(*ObjectName), RF_Public | RF_Standalone);

	FSkeletalMeshImportData SkelMeshImportData;

	FStaticMeshAttributes Attributes{ InMeshDescription };
	TVertexAttributesConstRef<FVector> VertexPositions = Attributes.GetVertexPositions();

	TVertexInstanceAttributesConstRef<FVector2D> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
	TVertexInstanceAttributesConstRef<FVector> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesConstRef<FVector> VertexInstanceTangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesConstRef<float> VertexInstanceBiNormalSigns = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesConstRef<FVector4> VertexInstanceColors = Attributes.GetVertexInstanceColors();

	TPolygonGroupAttributesConstRef<FName> PolygonGroupMaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
	//Get the per face smoothing
	TArray<uint32> FaceSmoothingMasks;
	FaceSmoothingMasks.AddZeroed(InMeshDescription.Triangles().Num());
	//////////////////////////////////////////////////////////////////////////
	// Copy the materials
	SkelMeshImportData.Materials.Reserve(InMeshDescription.PolygonGroups().Num());
	for (FPolygonGroupID PolygonGroupID : InMeshDescription.PolygonGroups().GetElementIDs())
	{
		SkeletalMeshImportData::FMaterial Material;
		Material.MaterialImportName = PolygonGroupMaterialSlotNames[PolygonGroupID].ToString();
		//The material interface will be added later by the factory
		SkelMeshImportData.Materials.Add(Material);
	}
	SkelMeshImportData.MaxMaterialIndex = SkelMeshImportData.Materials.Num() - 1;
	//Reserve the point and influences
	SkelMeshImportData.Points.AddZeroed(InMeshDescription.Vertices().Num());
	SkelMeshImportData.Influences.Reserve(InMeshDescription.Vertices().Num() * 4);
	for (size_t i = 0; i < InMesh->Surfaces.Num(); i++)
	{
		for (size_t w = 0; w < InMesh->Surfaces[i]->Vertexes.Num(); w++)
		{
			auto weights = InMesh->Surfaces[i]->Vertexes[w].Weights;
			for (size_t d = 0; d < weights.Num(); d++)
			{
				auto weight = weights[d];
				if (weight.WeightValue > 0)
				{
					SkeletalMeshImportData::FRawBoneInfluence Influence;
					Influence.BoneIndex = weight.WeightID;
					Influence.VertexIndex = weight.VertexIndex;
					Influence.Weight = weight.WeightValue;
					SkelMeshImportData.Influences.Add(Influence);
				}
			}
		}
	}
	for (FVertexID VertexID : InMeshDescription.Vertices().GetElementIDs())
	{
		//We can use GetValue because the Meshdescription was compacted before the copy
		SkelMeshImportData.Points[VertexID.GetValue()] = VertexPositions[VertexID];
		SkelMeshImportData.PointToRawMap.Add(SkelMeshImportData.Points.Num() - 1);


	}
	SkelMeshImportData.Faces.AddZeroed(InMeshDescription.Triangles().Num());
	SkelMeshImportData.Wedges.Reserve(InMeshDescription.VertexInstances().Num());
	SkelMeshImportData.NumTexCoords = FMath::Min<int32>(VertexInstanceUVs.GetNumElements(), (int32)MAX_TEXCOORDS);
	for (FTriangleID TriangleID : InMeshDescription.Triangles().GetElementIDs())
	{
		FPolygonGroupID PolygonGroupID = InMeshDescription.GetTrianglePolygonGroup(TriangleID);
		TArrayView<const FVertexInstanceID> VertexInstances = InMeshDescription.GetTriangleVertexInstances(TriangleID);
		int32 FaceIndex = TriangleID.GetValue();
		if (!ensure(SkelMeshImportData.Faces.IsValidIndex(FaceIndex)))
		{
			//TODO log an error for the user
			break;
		}
		SkeletalMeshImportData::FTriangle& Face = SkelMeshImportData.Faces[FaceIndex];
		Face.MatIndex = PolygonGroupID.GetValue();
		Face.SmoothingGroups = 0;
		if (FaceSmoothingMasks.IsValidIndex(FaceIndex))
		{
			Face.SmoothingGroups = FaceSmoothingMasks[FaceIndex];
		}
		//Create the wedges
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			FVertexInstanceID VertexInstanceID = VertexInstances[Corner];
			SkeletalMeshImportData::FVertex Wedge;
			Wedge.VertexIndex = (uint32)InMeshDescription.GetVertexInstanceVertex(VertexInstances[Corner]).GetValue();
			Wedge.MatIndex = Face.MatIndex;
			const bool bSRGB = false; //avoid linear to srgb conversion
			Wedge.Color = FLinearColor(VertexInstanceColors[VertexInstanceID]).ToFColor(bSRGB);
			if (Wedge.Color != FColor::White)
			{
				SkelMeshImportData.bHasVertexColors = true;
			}
			for (int32 UVChannelIndex = 0; UVChannelIndex < (int32)(2); ++UVChannelIndex)
			{
				Wedge.UVs[UVChannelIndex] = VertexInstanceUVs.Get(VertexInstanceID, UVChannelIndex);
			}
			Face.TangentX[Corner] = VertexInstanceTangents[VertexInstanceID];
			Face.TangentZ[Corner] = VertexInstanceNormals[VertexInstanceID];
			Face.TangentY[Corner] = FVector::CrossProduct(VertexInstanceNormals[VertexInstanceID], VertexInstanceTangents[VertexInstanceID]).GetSafeNormal() * VertexInstanceBiNormalSigns[VertexInstanceID];
			Face.WedgeIndex[Corner] = SkelMeshImportData.Wedges.Add(Wedge);
		}
		Swap(Face.WedgeIndex[0], Face.WedgeIndex[2]);
		Swap(Face.TangentZ[0], Face.TangentZ[2]);
	}
	for (auto PskBone : InMesh->Bones)
	{
		SkeletalMeshImportData::FBone Bone;
		Bone.Name = PskBone.Name;
		Bone.ParentIndex = PskBone.ParentIndex == -1 ? INDEX_NONE : PskBone.ParentIndex;

		FVector PskBonePos = PskBone.LocalPosition;
		FQuat PskBoneRot = PskBone.LocalRotation.Quaternion();
		//if (PskBone.LocalPosition != FVector::ZeroVector || PskBone.LocalRotation != FQuat::Identity)
		//{
			//PskBonePos = PskBone.LocalPosition;
			//PskBoneRot = PskBone.LocalRotation;
		//}
		FTransform PskTransform;

		PskTransform.SetLocation(PskBonePos);
		PskTransform.SetRotation(PskBoneRot);
		//PskTransform.SetRotation(FQuat(PskBoneRot.X, -PskBoneRot.Y, PskBoneRot.Z, (PskBone.ParentIndex == -1) ? -PskBoneRot.W : PskBoneRot.W));

		SkeletalMeshImportData::FJointPos BonePos;
		BonePos.Transform = PskTransform;

		Bone.BonePos = BonePos;
		SkelMeshImportData.RefBonesBinary.Add(Bone);
	}
	auto newName = "SK_" + ObjectName;
	USkeleton* Skeleton = NewObject<USkeleton>(ParentPackage, FName(*newName), RF_Public | RF_Standalone);
	FReferenceSkeleton RefSkeleton;
	auto SkeletalDepth = 0;
	ProcessSkeleton(SkelMeshImportData, Skeleton, RefSkeleton, SkeletalDepth);

	TArray<FVector> LODPoints;
	TArray<SkeletalMeshImportData::FMeshWedge> LODWedges;
	TArray<SkeletalMeshImportData::FMeshFace> LODFaces;
	TArray<SkeletalMeshImportData::FVertInfluence> LODInfluences;
	TArray<int32> LODPointToRawMap;
	SkelMeshImportData.CopyLODImportData(LODPoints, LODWedges, LODFaces, LODInfluences, LODPointToRawMap);

	FSkeletalMeshLODModel LODModel;
	LODModel.NumTexCoords = FMath::Max<uint32>(1, SkelMeshImportData.NumTexCoords);

	//SkelMeshImportData->PreEditChange(nullptr);
	SkeletalMesh->InvalidateDeriveDataCacheGUID(); 
	SkeletalMesh->UnregisterAllMorphTarget();

	SkeletalMesh->GetRefBasesInvMatrix().Empty();
	SkeletalMesh->GetMaterials().Empty();
	SkeletalMesh->SetHasVertexColors(true);

	FSkeletalMeshModel* ImportedResource = SkeletalMesh->GetImportedModel();
	auto& SkeletalMeshLODInfos = SkeletalMesh->GetLODInfoArray();
	SkeletalMeshLODInfos.Empty();
	SkeletalMeshLODInfos.Add(FSkeletalMeshLODInfo());
	SkeletalMeshLODInfos[0].ReductionSettings.NumOfTrianglesPercentage = 1.0f;
	SkeletalMeshLODInfos[0].ReductionSettings.NumOfVertPercentage = 1.0f;
	SkeletalMeshLODInfos[0].ReductionSettings.MaxDeviationPercentage = 0.0f;
	SkeletalMeshLODInfos[0].LODHysteresis = 0.02f;

	ImportedResource->LODModels.Empty();
	ImportedResource->LODModels.Add(new FSkeletalMeshLODModel);
	SkeletalMesh->SetRefSkeleton(RefSkeleton);
	SkeletalMesh->CalculateInvRefMatrices();

	SkeletalMesh->SaveLODImportedData(0, SkelMeshImportData);
	FSkeletalMeshBuildSettings BuildOptions;
	BuildOptions.bRemoveDegenerates = true;
	BuildOptions.bRecomputeTangents = true;
	BuildOptions.bUseMikkTSpace = true;
	SkeletalMesh->GetLODInfo(0)->BuildSettings = BuildOptions;
	SkeletalMesh->SetImportedBounds(FBoxSphereBounds(FBoxSphereBounds(FBox(SkelMeshImportData.Points))));
	TArray<FSkeletalMaterial> SkelMats;
	// Create materials and mesh sections
	for (int i = 0; i < InMesh->Surfaces.Num(); i++)
	{
		const auto Surface = InMesh->Surfaces[i];

		// Static Material for Surface
		FSkeletalMaterial&& UEMat = FSkeletalMaterial(UMaterial::GetDefaultMaterial(MD_Surface));
		//FStaticMaterial&& UEMat = FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface));
		if (Surface->Materials.Num() > 0)
		{
			// Create an array of Surface Materials
			TArray<C2Material*> SurfMaterials;
			SurfMaterials.Reserve(Surface->Materials.Num());
			for (const uint16_t MaterialIndex : Surface->Materials)
				SurfMaterials.Push(CoDMaterials[MaterialIndex]);
			UMaterialInterface* MaterialInstance = C2MMaterialInstance::CreateMixMaterialInstance(SurfMaterials,ParentPackage);
			UEMat = FSkeletalMaterial(MaterialInstance);
		}
		UEMat.UVChannelData.bInitialized = true;
		UEMat.MaterialSlotName = FName(Surface->Name);
		UEMat.ImportedMaterialSlotName = FName(Surface->Name);
		SkelMats.Add(UEMat);
	}
	SkeletalMesh->SetMaterials(SkelMats);

	auto& MeshBuilderModule = IMeshBuilderModule::GetForRunningPlatform();
	const FSkeletalMeshBuildParameters SkeletalMeshBuildParameters(SkeletalMesh, GetTargetPlatformManagerRef().GetRunningTargetPlatform(), 0, false);
	if (!MeshBuilderModule.BuildSkeletalMesh(SkeletalMeshBuildParameters))
	{
		SkeletalMesh->BeginDestroy();
		return nullptr;
	}

	for (auto Material : SkelMeshImportData.Materials)
	{
		SkeletalMesh->GetMaterials().Add(FSkeletalMaterial(Material.Material.Get()));
	}

	SkeletalMesh->PostEditChange();

	SkeletalMesh->SetSkeleton(Skeleton);
	Skeleton->MergeAllBonesToBoneTree(SkeletalMesh);
	Skeleton->SetPreviewMesh(SkeletalMesh);
	FAssetRegistryModule::AssetCreated(SkeletalMesh);
	SkeletalMesh->MarkPackageDirty();

	Skeleton->PostEditChange();
	FAssetRegistryModule::AssetCreated(Skeleton);
	Skeleton->MarkPackageDirty();
	return SkeletalMesh;
}
