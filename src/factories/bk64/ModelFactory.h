#pragma once

#include <factories/BaseFactory.h>

namespace BK64 {

/**
 * BoneData: Skeletal animation bone joint
 * 
 * Runtime Purpose:
 * - Defines bone hierarchy for animated 3D models (Banjo, Kazooie, enemies, NPCs)
 * - id specifies bone index, parentId links to parent bone (-1 for root)
 * - x, y, z are bone offsets from parent in local space
 * - Used by animation system to apply keyframe rotations/translations
 */
typedef struct BoneData {
    float x, y, z;         // Bone position offset from parent
    uint16_t id;           // Bone index
    uint16_t parentId;     // Parent bone index (0xFFFF = root)
} BoneData;

/**
 * GeoCube: Collision detection spatial partition cell
 * 
 * Runtime Purpose:
 * - Divides model collision mesh into grid cells for fast intersection tests
 * - Each cube contains subset of triangles determined by AABB overlap
 * - Player position → grid cell lookup → test only triangles in that cube
 * - Dramatically reduces collision checks from thousands to dozens
 */
typedef struct GeoCube {
    uint16_t startTri;     // Index of first triangle in this cube
    uint16_t triCount;     // Number of triangles in this cube
} GeoCube;

/**
 * CollisionTri: Triangle face for collision detection
 * 
 * Runtime Purpose:
 * - Defines collision geometry (floors, walls, ceilings, slopes)
 * - vtxId1/2/3 index into vertex array (defines triangle shape)
 * - flags determines surface type: walkable floor, wall, water, lava, ice, etc.
 * - Used by func_80309CF8_rayTriIntersect for player/actor collision response
 * 
 * Flag Types (examples):
 *   0x01: Walkable floor
 *   0x02: Wall (blocks movement)
 *   0x04: Water surface
 *   0x08: Damage surface (lava, spikes)
 *   0x10: Ice (reduced friction)
 */
typedef struct CollisionTri {
    uint16_t vtxId1, vtxId2, vtxId3;  // Vertex indices forming triangle
    uint16_t unk6;                     // Additional flags/material ID
    uint32_t flags;                    // Surface type flags (walkable, water, damage, etc.)
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
 * 
 * Runtime Purpose:
 * - Cycles through texture frames for animated surfaces (water, lava, scrolling)
 * - frameSize: bytes per texture frame
 * - frameCount: total animation frames
 * - frameRate: frames per second for playback
 * - Current frame = (gameTime * frameRate) % frameCount
 */
typedef struct AnimTexture {
    uint16_t frameSize;    // Bytes per texture frame
    uint16_t frameCount;   // Number of animation frames
    float frameRate;       // Animation speed (frames per second)
} AnimTexture;

/**
 * CollisionHeader: Collision detection grid parameters
 * 
 * Runtime Purpose:
 * - Defines bounding box and grid stride for GeoCube spatial partitioning
 * - minIndex/maxIndex: AABB containing all collision geometry
 * - yStride/zStride: number of cubes per row/layer (grid dimensions)
 * - geoCubeScale: world units per cube edge (typically 100-200)
 * 
 * Grid Cell Calculation:
 *   cubeX = (worldX - minIndexX) / geoCubeScale
 *   cubeY = (worldY - minIndexY) / geoCubeScale
 *   cubeZ = (worldZ - minIndexZ) / geoCubeScale
 *   cubeIndex = cubeX + cubeY * yStride + cubeZ * yStride * zStride
 */
typedef struct CollisionHeader {
    int16_t minIndexX, minIndexY, minIndexZ;  // AABB min corner
    int16_t maxIndexX, maxIndexY, maxIndexZ;  // AABB max corner
    uint16_t yStride;       // Cubes per row (X dimension)
    uint16_t zStride;       // Cubes per layer (X × Y grid)
    uint16_t geoCubeScale;  // World units per cube
} CollisionHeader;

/**
 * AnimationHeader: Animation scaling factor
 * 
 * Runtime Purpose:
 * - Scales bone animation keyframe values to world space
 * - Animation data stored as integers, multiplied by scalingFactor for floats
 * - Allows compact storage of animation data
 */
typedef struct AnimationHeader {
    float scalingFactor;   // Multiplier for animation keyframe values
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
