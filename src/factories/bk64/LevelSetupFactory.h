#pragma once

#include <factories/BaseFactory.h>
#include <vector>

namespace BK64 {

/**
 * NodeProp: Spawn point, warp, trigger, or event marker (20 bytes)
 * 
 * Categories (bit6): 6=Actor spawn, 7=Warp, 9=Trigger, 0xA=Event
 * Processed at level load to create ActorMarkers and trigger points
 */
typedef struct NodeProp {
    int16_t x, y, z;
    uint16_t radius: 9;
    uint16_t bit6: 6;       // 6=actor, 7=warp, 9=trigger, 0xA=event
    uint16_t bit0: 1;
    uint16_t unk8;          // Actor/Warp/Event ID
    uint8_t unkA;           // Marker ID
    uint8_t padB;
    uint32_t yaw: 9;        // *2 for degrees
    uint32_t scale: 23;     // /1000.0 for actual scale
    uint32_t unk10_31: 12;
    uint32_t unk10_19: 12;
    uint32_t pad10_7: 1;
    uint32_t unk10_6: 1;
    uint32_t pad10_5: 4;
    uint32_t unk10_0: 2;
} NodeProp;

/**
 * ModelProp: Static 3D model (is_actor=0, is_3d=1) - 12 bytes
 * Asset ID = (unk0 & 0xFFF) + 0x2D1
 * Yaw/Roll *2 for degrees, Scale /100.0 for actual scale
 */
typedef struct ModelProp {
    uint16_t unk0;          // model_index:12, pad:4
    uint8_t yaw;
    uint8_t roll;
    int16_t position[3];
    uint8_t scale;
    uint8_t flags;
} ModelProp;

/**
 * SpriteProp: 2D billboard sprite (is_actor=0, is_3d=0) - 12 bytes
 * Asset ID = (word0 & 0xFFF) + 0x572
 * RGB at bits 13-21 (r:13-15, b:16-18, g:19-21), Scale /100.0
 */
typedef struct SpriteProp {
    uint32_t word0;         // sprite_index:12, unk0_19:1, r:3, b:3, g:3, scale:8, mirrored:1
    int16_t unk4[3];
    uint16_t word8;         // frame:5, unk8_10:5, flags:6
} SpriteProp;

/**
 * ActorProp: Dynamic entity created at runtime (is_actor=1) - 12 bytes
 * Created by func_8032F21C when actors spawn from NodeProp definitions.
 * ROM contains only ModelProp and SpriteProp.
 */
typedef struct ActorProp {
    uint32_t marker;        // ActorMarker* (runtime only)
    int16_t x, y, z;        // Position cache
    uint16_t flags;
} ActorProp;

/**
 * Prop: Union of ModelProp, SpriteProp, ActorProp (12 bytes)
 * 
 * Type discrimination (flags at offset 0xA):
 *   is_actor=1 → ActorProp (runtime only)
 *   is_actor=0, is_3d=1 → ModelProp
 *   is_actor=0, is_3d=0 → SpriteProp
 */
typedef union Prop {
    ModelProp model;
    SpriteProp sprite;
    ActorProp actor;
    struct {
        uint32_t pad0;
        int16_t unk4[3];
        uint16_t pad8_15: 10;
        uint16_t unk8_5: 1;
        uint16_t unk8_4: 1;
        uint16_t unk8_3: 1;
        uint16_t unk8_2: 1;
        uint16_t is_3d: 1;
        uint16_t is_actor: 1;
    };
    uint8_t raw[12];
} Prop;

/**
 * CubeData: Spatial partition cell in 32×32×32 grid
 * 
 * Grid coordinates: 5-bit values (0-31) for x/y/z
 * unk0_4: Boundary separating regular NodeProps [0..unk0_4) from events [unk0_4..prop1Cnt)
 */
typedef struct CubeData {
    int32_t x: 5;
    int32_t y: 5;
    int32_t z: 5;
    uint32_t prop1Cnt: 6;
    uint32_t prop2Cnt: 6;
    uint32_t unk0_4: 5;     // Boundary: [0..unk0_4)=spawns, [unk0_4..prop1Cnt)=events
    std::vector<NodeProp> nodeProps;
    std::vector<Prop> props;
} CubeData;

class LevelSetupData : public IParsedData {
  public:
    std::vector<CubeData> mCubes;

    LevelSetupData(std::vector<CubeData> cubes) : mCubes(std::move(cubes)) {}
};

class LevelSetupHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class LevelSetupBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class LevelSetupCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class LevelSetupModdingExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class LevelSetupFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { 
            REGISTER(Code, LevelSetupCodeExporter) 
            REGISTER(Header, LevelSetupHeaderExporter)                     
            REGISTER(Binary, LevelSetupBinaryExporter)
            REGISTER(Modding, LevelSetupModdingExporter)
        };
    }
    
    bool HasModdedDependencies() override { return true; }
};
} // namespace BK64
