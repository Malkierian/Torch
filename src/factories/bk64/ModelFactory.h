#pragma once

#include <factories/BaseFactory.h>

namespace BK64 {

/**
 * BoneData: Skeletal animation bone joint
 * Decomp: BKAnimation from model.h
 * 
 * Structure Layout:
 *   Offset 0x00: unk0[3] (f32[3]) - X, Y, Z bone position offset from parent
 *   Offset 0x0C: bone_id (s16) - Bone index
 *   Offset 0x0E: mtx_id (s16) - Parent bone matrix ID
 */
typedef struct BoneData {
    float pos[3];          // unk0[3]: Bone position offset from parent [X, Y, Z]
    uint16_t id;           // bone_id: Bone index
    uint16_t parentId;     // mtx_id: Parent bone matrix ID (0xFFFF = root)
} BoneData;

/**
 * GeoCube: Collision detection spatial partition cell
 * Decomp: BKCollisionGeo from model.h
 * 
 * Structure Layout:
 *   Offset 0x00: start_tri_index (s16) - Index of first triangle in this cube
 *   Offset 0x02: tri_count (s16) - Number of triangles in this cube
 */
typedef struct GeoCube {
    uint16_t startTri;     // start_tri_index: Index of first triangle in this cube
    uint16_t triCount;     // tri_count: Number of triangles in this cube
} GeoCube;

/**
 * CollisionTri: Triangle face for collision detection
 * Decomp: BKCollisionTri from model.h
 * 
 * Structure Layout:
 *   Offset 0x00: unk0[3] (s16[3]) - Vertex indices (we name it vtxIds[3])
 *   Offset 0x06: unk6 (s16) - Additional flags/material ID
 *   Offset 0x08: flags (s32) - Surface type flags
 */
typedef struct CollisionTri {
    uint16_t vtxIds[3];    // Vertex indices forming triangle (decomp: unk0[3])
    uint16_t unk6;         // Additional flags/material ID
    uint32_t flags;        // Surface type flags (walkable, water, damage, etc.)
} CollisionTri;

/**
 * Effect: Vertex group for special rendering effects
 */
typedef struct Effect {
    uint16_t dataInfo;               // Effect type and parameters
    std::vector<uint16_t> vtxIndices;  // Vertices affected by this effect
} Effect;

/**
 * AnimTexture: Animated texture definition
 * Decomp: AnimTexture from model.h
 * 
 * Structure Layout (decomp field names):
 *   Offset 0x00: frame_size (s16) - Bytes per texture frame
 *   Offset 0x02: frame_cnt (s16) - Number of animation frames
 *   Offset 0x04: frame_rate (f32) - Animation speed (frames per second)
 */
typedef struct AnimTexture {
    uint16_t frameSize;    // Bytes per texture frame (decomp: frame_size)
    uint16_t frameCount;   // Number of animation frames (decomp: frame_cnt)
    float frameRate;       // Animation speed in fps (decomp: frame_rate)
} AnimTexture;

/**
 * CollisionHeader: Collision detection grid parameters
 * Decomp: BKCollisionList from model.h
 * 
 * Structure Layout (decomp field names):
 *   Offset 0x00: unk0[3] (s16[3]) - min[X,Y,Z]
 *   Offset 0x06: unk6[3] (s16[3]) - max[X,Y,Z]
 *   Offset 0x0C: unkC (s16) - y_stride
 *   Offset 0x0E: unkE (s16) - z_stride
 *   Offset 0x10: unk10 (s16) - geo_cnt (we calculate from vector.size())
 *   Offset 0x12: unk12 (s16) - scale
 *   Offset 0x14: unk14 (s16) - tri_cnt (we calculate from vector.size())
 * 
 * Grid Cell Calculation:
 *   cubeX = (worldX - minX) / geoCubeScale
 *   cubeY = (worldY - minY) / geoCubeScale
 *   cubeZ = (worldZ - minZ) / geoCubeScale
 *   cubeIndex = cubeX + cubeY * yStride + cubeZ * yStride * zStride
 */
typedef struct CollisionHeader {
    int16_t minIndex[3];   // AABB min corner [X, Y, Z] (decomp: unk0[3])
    int16_t maxIndex[3];   // AABB max corner [X, Y, Z] (decomp: unk6[3])
    uint16_t yStride;      // Cubes per row (decomp: unkC/y_stride)
    uint16_t zStride;      // Cubes per layer (decomp: unkE/z_stride)
    uint16_t geoCubeScale; // World units per cube (decomp: unk12/scale)
} CollisionHeader;

/**
 * AnimationHeader: Animation scaling factor
 * Decomp: BKAnimationList from model.h
 * 
 * Structure Layout:
 *   Offset 0x00: unk0 (f32) - Scaling multiplier for animation keyframes
 *   Offset 0x04: cnt_4 (s16) - Number of bones (we calculate from vector.size())
 */
typedef struct AnimationHeader {
    float scalingFactor;   // unk0: Multiplier for animation keyframe values
} AnimationHeader;

class ModelData : public IParsedData {
  public:
    uint16_t mGeoType;
    uint16_t mTriCount;
    uint16_t mVertCount;
    
    // Animation data
    bool mHasAnimation;
    AnimationHeader mAnimHeader;
    std::vector<BoneData> mBones;
    
    // Collision data
    bool mHasCollision;
    CollisionHeader mCollisionHeader;
    std::vector<GeoCube> mGeoCubes;
    std::vector<CollisionTri> mCollisionTris;
    
    // Effects
    std::vector<Effect> mEffects;
    
    // Animated textures
    std::vector<AnimTexture> mAnimTextures;

    ModelData(uint16_t geoType, uint16_t triCount, uint16_t vertCount)
        : mGeoType(geoType), mTriCount(triCount), mVertCount(vertCount),
          mHasAnimation(false), mHasCollision(false) {}
};

class ModelHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class ModelBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class ModelCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class ModelFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { 
            REGISTER(Code, ModelCodeExporter) 
            REGISTER(Header, ModelHeaderExporter)                     
            REGISTER(Binary, ModelBinaryExporter) 
        };
    }

    bool HasModdedDependencies() override { return true; }
};
} // namespace BK64
