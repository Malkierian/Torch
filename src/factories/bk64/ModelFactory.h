#pragma once

#include <factories/BaseFactory.h>

namespace BK64 {

/**
 * BoneData: Skeletal animation bone joint
 * Decomp: BKAnimation from model.h
 * 
 * Runtime Purpose:
 * - Defines bone hierarchy for animated 3D models (Banjo, Kazooie, enemies, NPCs)
 * - bone_id specifies bone index, mtx_id links to parent bone matrix
 * - unk0[3] are bone offsets from parent in local space (f32[3])
 * - Used by animation system to apply keyframe rotations/translations
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
 * Runtime Purpose:
 * - Divides model collision mesh into grid cells for fast intersection tests
 * - Each cube contains subset of triangles determined by AABB overlap
 * - Player position → grid cell lookup → test only triangles in that cube
 * - Dramatically reduces collision checks from thousands to dozens
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
 * Runtime Purpose:
 * - Defines collision geometry (floors, walls, ceilings, slopes)
 * - vtxIds[3] index into vertex array (defines triangle shape)
 * - flags determines surface type: walkable floor, wall, water, lava, ice, etc.
 * - Used by func_80309CF8_rayTriIntersect for player/actor collision response
 * 
 * Structure Layout:
 *   Offset 0x00: unk0[3] (s16[3]) - Vertex indices (we name it vtxIds[3])
 *   Offset 0x06: unk6 (s16) - Additional flags/material ID
 *   Offset 0x08: flags (s32) - Surface type flags
 * 
 * Flag Types (examples):
 *   0x01: Walkable floor
 *   0x02: Wall (blocks movement)
 *   0x04: Water surface
 *   0x08: Damage surface (lava, spikes)
 *   0x10: Ice (reduced friction)
 */
typedef struct CollisionTri {
    uint16_t vtxIds[3];    // Vertex indices forming triangle (decomp: unk0[3])
    uint16_t unk6;         // Additional flags/material ID
    uint32_t flags;        // Surface type flags (walkable, water, damage, etc.)
} CollisionTri;

/**
 * Effect: Vertex group for special rendering effects
 * 
 * Runtime Purpose:
 * - Defines vertex subsets for effects like water ripples, transparency, glow
 * - dataInfo specifies effect type and parameters
 * - vtxIndices lists affected vertices
 * - Processed during rendering to apply shader effects or animations
 */
typedef struct Effect {
    uint16_t dataInfo;               // Effect type and parameters
    std::vector<uint16_t> vtxIndices;  // Vertices affected by this effect
} Effect;

/**
 * AnimTexture: Animated texture definition
 * Decomp: AnimTexture from model.h
 * 
 * Runtime Purpose:
 * - Cycles through texture frames for animated surfaces (water, lava, scrolling)
 * - Current frame = (gameTime * frameRate) % frameCount
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
 * Runtime Purpose:
 * - Defines bounding box and grid stride for GeoCube spatial partitioning
 * - minIndex/maxIndex: AABB containing all collision geometry
 * - yStride/zStride: number of cubes per row/layer (grid dimensions)
 * - geoCubeScale: world units per cube edge (typically 100-200)
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
 * Runtime Purpose:
 * - Scales bone animation keyframe values to world space
 * - Animation data stored as integers, multiplied by scalingFactor for floats
 * - Allows compact storage of animation data
 * 
 * Structure Layout:
 *   Offset 0x00: unk0 (f32) - Scaling multiplier for animation keyframes
 *   Offset 0x04: cnt_4 (s16) - Number of bones (we calculate from vector.size())
 */
typedef struct AnimationHeader {
    float scalingFactor;   // unk0: Multiplier for animation keyframe values
} AnimationHeader;
    
namespace Model {

} // namespace Model
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
