#pragma once

#include <factories/BaseFactory.h>
#include <vector>

namespace BK64 {

// Forward declare NodeProp from MapFactory.h
struct NodeProp;

class NodePropArrayData : public IParsedData {
  public:
    std::vector<NodeProp> mNodeProps;
    std::string mParentSymbol;
    size_t mCubeIndex;

    NodePropArrayData(std::vector<NodeProp> nodeProps, std::string parentSymbol, size_t cubeIndex)
        : mNodeProps(std::move(nodeProps)), mParentSymbol(parentSymbol), mCubeIndex(cubeIndex) {}
};

class NodePropArrayHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class NodePropArrayBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class NodePropArrayCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class NodePropArrayFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code, NodePropArrayCodeExporter)
            REGISTER(Header, NodePropArrayHeaderExporter)
            REGISTER(Binary, NodePropArrayBinaryExporter)
        };
    }
};

} // namespace BK64
