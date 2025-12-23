#pragma once

#include <factories/BaseFactory.h>

namespace BK64 {

typedef struct BoneData {
    float x;
    float y;
    float z;
    uint16_t id;
    uint16_t parentId;
} BoneData;

typedef struct GeoCube {
    uint16_t startTri;
    uint16_t triCount;
} GeoCube;

typedef struct CollisionTri {
    uint16_t vtxId1;
    uint16_t vtxId2;
    uint16_t vtxId3;
    uint16_t unk6;
    uint32_t flags;
} CollisionTri;

typedef struct Effect {
    uint16_t dataInfo;
    std::vector<uint16_t> vtxIndices;
} Effect;

typedef struct AnimTexture {
    uint16_t frameSize;
    uint16_t frameCount;
    float frameRate;
} AnimTexture;
    
namespace Model {

} // namespace Model
class ModelData : public IParsedData {
  public:

    ModelData() {}
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
