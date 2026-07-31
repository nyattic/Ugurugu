#pragma once

#include "document/Document.hpp"

#include <QCoreApplication>

#include <optional>

namespace wobble {

class DocumentSerializer
{
    Q_DECLARE_TR_FUNCTIONS(wobble::DocumentSerializer)

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
