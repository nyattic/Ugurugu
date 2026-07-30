#pragma once

#include "document/Document.hpp"

#include <optional>

namespace wobble {

class DocumentSerializer
{
public:
    static bool save(
        const QString &filePath,
        const Document &document,
        QString *error = nullptr);
    static std::optional<Document> load(
        const QString &filePath,
        QString *error = nullptr);
    static QByteArray toJson(const Document &document);
    static std::optional<Document> fromJson(
        const QByteArray &data,
        QString *error = nullptr);
};

}
