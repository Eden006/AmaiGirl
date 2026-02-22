#include "engine/Live2DCore.hpp"
#include <cstring>

static void allocAlignedBuffer(QByteArray &storage, void* &alignedPtr, size_t size, int align) {
    size_t padded = size + align;
    storage = QByteArray((int)padded, '\0');
    uintptr_t base = reinterpret_cast<uintptr_t>(storage.data());
    uintptr_t aligned = (base + (align - 1)) & ~(uintptr_t)(align - 1);
    alignedPtr = reinterpret_cast<void*>(aligned);
}

QString Live2DCore::versionString() {
    uint32_t code = csmGetVersion();
    uint32_t major = code >> 24;
    uint32_t minor = (code >> 16) & 0xFF;
    uint32_t patch = code & 0xFFFF;
    return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
}

uint32_t Live2DCore::latestMocVersion() { return csmGetLatestMocVersion(); }

uint32_t Live2DCore::mocVersion(const QByteArray &data) {
    return csmGetMocVersion((void*)data.data(), static_cast<uint32_t>(data.size()));
}

MocHolder Live2DCore::loadMoc(const QString &path) {
    MocHolder h;
    QByteArray in = readFileAll(path);

    // Copy to aligned moc buffer
    allocAlignedBuffer(h.mocStorage, h.mocAlignedPtr, in.size(), csmAlignofMoc);
    std::memcpy(h.mocAlignedPtr, in.constData(), (size_t)in.size());

    ensure(csmHasMocConsistency(h.mocAlignedPtr, (uint32_t)in.size()) == 1,
           QString("moc not consistency: %1").arg(path));
    auto maxV = latestMocVersion();
    auto curV = mocVersion(in);
    ensure(curV != 0 && curV <= maxV, QString("core %1 not support version %2").arg(maxV).arg(curV));

    h.moc = csmReviveMocInPlace(h.mocAlignedPtr, (uint32_t)in.size());

    size_t modelSize = csmGetSizeofModel(h.moc);
    ensure(modelSize > 0, "moc load fail");

    allocAlignedBuffer(h.modelStorage, h.modelAlignedPtr, modelSize, csmAlignofModel);
    h.model = csmInitializeModelInPlace(h.moc, h.modelAlignedPtr, modelSize);
    return h;
}
