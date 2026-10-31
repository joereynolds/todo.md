#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <vector>

// A replacement of a range of lines in one file. `before` is verified against
// the file before anything is written, so edits never clobber external changes.
//   replace: before = {old},  after = {new}
//   insert:  before = {anchor}, after = {anchor, new}
//   delete:  before = {old},  after = {}
//   append:  line = -1, before = {}, after = {new}
struct Edit {
    QString path;
    int line = 0;
    QStringList before;
    QStringList after;
};

// File contents split into lines, preserving line endings byte for byte.
struct LineBuffer {
    QList<QByteArray> lines; // without '\n'; a trailing '\r' is kept
    bool crlf = false;       // file uses CRLF; applied to new lines
    bool trailingNewline = true;

    static LineBuffer fromData(const QByteArray &data);
    QByteArray toData() const;
};

struct ApplyResult {
    int applied = 0;
    int conflicts = 0;           // edits whose `before` did not match
    std::vector<Edit> inverse;   // undoes the applied edits (positions valid afterwards)
};

// Applies the edits for a single file. Overlapping edits after the first are
// counted as conflicts.
ApplyResult applyEdits(LineBuffer &buf, std::vector<Edit> edits);
