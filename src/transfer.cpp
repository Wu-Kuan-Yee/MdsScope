// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/mdsscope_internal.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFileInfo>
#include <QTextStream>

namespace {
QString outputPathFor(const QString& inputPath, const QString& outputDir)
{
    const QFileInfo info(inputPath);
    const QString fileName = info.completeBaseName() + ".toml";
    if (!outputDir.isEmpty()) {
        return QDir(outputDir).filePath(fileName);
    }
    return QDir(info.absolutePath()).filePath(fileName);
}

bool convertOne(const QString& inputPath, const QString& outputDir, QTextStream& out, QTextStream& err)
{
    const QFileInfo inputInfo(inputPath);
    if (!inputInfo.exists() || !inputInfo.isFile()) {
        err << "skip: not a file: " << inputPath << Qt::endl;
        return false;
    }
    if (inputInfo.suffix().compare("webscp", Qt::CaseInsensitive) != 0) {
        err << "skip: not a .webscp file: " << inputPath << Qt::endl;
        return false;
    }

    LayoutConfig config = parseEnvironment(inputInfo.absoluteFilePath());
    const QString outputPath = outputPathFor(inputInfo.absoluteFilePath(), outputDir);
    QString error;
    if (!writeEnvironmentToml(config, outputPath, &error)) {
        err << "failed: " << inputPath << ": " << error << Qt::endl;
        return false;
    }
    out << inputInfo.absoluteFilePath() << " -> " << outputPath << Qt::endl;
    return true;
}

QStringList collectInputs(const QStringList& paths, bool recursive)
{
    QStringList files;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            QDirIterator it(info.absoluteFilePath(),
                            {"*.webscp"},
                            QDir::Files,
                            recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
            while (it.hasNext()) {
                files.push_back(it.next());
            }
        } else {
            files.push_back(path);
        }
    }
    files.sort(Qt::CaseInsensitive);
    return files;
}

void printUsage(QTextStream& out)
{
    out << "Usage: transfer [--recursive] [--out-dir DIR] <file.webscp|directory>...\n"
        << "\n"
        << "Examples:\n"
        << "  transfer environment/init.webscp\n"
        << "  transfer environment\n"
        << "  transfer --recursive --out-dir converted environment\n";
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    QString outputDir;
    bool recursive = false;
    QStringList inputs;
    const QStringList args = app.arguments().mid(1);
    for (int i = 0; i < args.size(); ++i) {
        const QString arg = args[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(out);
            return 0;
        }
        if (arg == "--recursive" || arg == "-r") {
            recursive = true;
            continue;
        }
        if (arg == "--out-dir" || arg == "-o") {
            if (i + 1 >= args.size()) {
                err << "missing value for " << arg << Qt::endl;
                return 2;
            }
            outputDir = args[++i];
            QDir().mkpath(outputDir);
            continue;
        }
        inputs.push_back(arg);
    }

    if (inputs.isEmpty()) {
        printUsage(err);
        return 2;
    }

    const QStringList files = collectInputs(inputs, recursive);
    if (files.isEmpty()) {
        err << "no .webscp files found" << Qt::endl;
        return 1;
    }

    int ok = 0;
    int failed = 0;
    for (const QString& file : files) {
        if (convertOne(file, outputDir, out, err)) {
            ++ok;
        } else {
            ++failed;
        }
    }
    out << "converted=" << ok << " failed=" << failed << Qt::endl;
    return failed == 0 ? 0 : 1;
}
