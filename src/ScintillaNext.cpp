/*
 * This file is part of Notepad Next.
 * Copyright 2019 Justin Dailey
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */


#include "ScintillaNext.h"
#include "Finder.h"
#include "ScintillaCommenter.h"

#include "ByteArrayUtils.h"
#include "uchardet.h"
#include <cinttypes>

#include <QDir>
#include <QMouseEvent>
#include <QSaveFile>
#include <QTextCodec>


inline const QByteArray BOM_UTF8    = QByteArray::fromHex("EFBBBF");
inline const QByteArray BOM_UTF16LE = QByteArray::fromHex("FFFE");
inline const QByteArray BOM_UTF16BE = QByteArray::fromHex("FEFF");

// "ANSI" on this platform means Simplified Chinese legacy encoding (GB18030, a superset of GBK)
static QTextCodec *ansiCodec()
{
    static QTextCodec *codec = QTextCodec::codecForName("GB18030");
    return codec;
}

static bool isValidUtf8Text(const QByteArray &data)
{
    QTextCodec *utf8 = QTextCodec::codecForName("UTF-8");
    QTextCodec::ConverterState state;
    utf8->toUnicode(data.constData(), data.size(), &state);
    return state.invalidChars == 0;
}

static QString decodeUtf16(const QByteArray &data, bool littleEndian)
{
    QString text;
    text.reserve(data.size() / 2);

    for (qsizetype i = 0; i + 1 < data.size(); i += 2) {
        const uchar b1 = static_cast<uchar>(data.at(i));
        const uchar b2 = static_cast<uchar>(data.at(i + 1));
        const char16_t unit = littleEndian
                ? static_cast<char16_t>(b1 | (b2 << 8))
                : static_cast<char16_t>(b2 | (b1 << 8));
        text.append(unit);
    }

    return text;
}

static QByteArray encodeUtf16(const QString &text, bool littleEndian)
{
    QByteArray data;
    data.resize(text.size() * 2);

    for (qsizetype i = 0; i < text.size(); ++i) {
        const char16_t unit = text.at(i).unicode();
        const char lo = static_cast<char>(unit & 0xFF);
        const char hi = static_cast<char>((unit >> 8) & 0xFF);
        data[i * 2 + 0] = littleEndian ? lo : hi;
        data[i * 2 + 1] = littleEndian ? hi : lo;
    }

    return data;
}

static ScintillaNext::Encoding detectEncoding(const QByteArray &data)
{
    if (data.startsWith(BOM_UTF16LE)) return ScintillaNext::Encoding::Utf16LeBom;
    if (data.startsWith(BOM_UTF16BE)) return ScintillaNext::Encoding::Utf16BeBom;
    if (data.startsWith(BOM_UTF8))    return ScintillaNext::Encoding::Utf8Bom;
    if (isValidUtf8Text(data))        return ScintillaNext::Encoding::Utf8;
    if (ansiCodec())                  return ScintillaNext::Encoding::Ansi;

    return ScintillaNext::Encoding::Utf8;
}

static QString decodeToText(const QByteArray &data, ScintillaNext::Encoding encoding)
{
    switch (encoding) {
    case ScintillaNext::Encoding::Ansi:
        if (ansiCodec()) {
            QTextCodec::ConverterState state;
            return ansiCodec()->toUnicode(data.constData(), data.size(), &state);
        }
        return QString::fromUtf8(data);
    case ScintillaNext::Encoding::Utf8:
        return QString::fromUtf8(data);
    case ScintillaNext::Encoding::Utf8Bom:
        if (data.startsWith(BOM_UTF8))
            return QString::fromUtf8(data.constData() + BOM_UTF8.size(), data.size() - BOM_UTF8.size());
        return QString::fromUtf8(data);
    case ScintillaNext::Encoding::Utf16LeBom:
        return decodeUtf16(data.startsWith(BOM_UTF16LE) ? data.mid(BOM_UTF16LE.size()) : data, true);
    case ScintillaNext::Encoding::Utf16BeBom:
        return decodeUtf16(data.startsWith(BOM_UTF16BE) ? data.mid(BOM_UTF16BE.size()) : data, false);
    }

    return QString();
}

static QByteArray encodeFromText(const QByteArray &utf8Data, ScintillaNext::Encoding encoding)
{
    switch (encoding) {
    case ScintillaNext::Encoding::Ansi:
        if (ansiCodec()) {
            return ansiCodec()->fromUnicode(QString::fromUtf8(utf8Data));
        }
        return utf8Data;
    case ScintillaNext::Encoding::Utf8:
        return utf8Data;
    case ScintillaNext::Encoding::Utf8Bom:
        return BOM_UTF8 + utf8Data;
    case ScintillaNext::Encoding::Utf16LeBom:
        return BOM_UTF16LE + encodeUtf16(QString::fromUtf8(utf8Data), true);
    case ScintillaNext::Encoding::Utf16BeBom:
        return BOM_UTF16BE + encodeUtf16(QString::fromUtf8(utf8Data), false);
    }

    return utf8Data;
}

static QFileDevice::FileError writeToDisk(const QByteArray &data, const QString &path)
{
    qInfo(Q_FUNC_INFO);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("writeToDisk() failed to open file %s: %s", qPrintable(path), qPrintable(file.errorString()));
        return file.error();
    }

    // Write actual data
    if (file.write(data) == -1) {
        qWarning("writeToDisk() failed writing data: %s", qPrintable(file.errorString()));
        return file.error();
    }

    return file.error();
}

static bool isNewlineCharacter(char c)
{
    return c == '\n' || c == '\r';
}

ScintillaNext::ScintillaNext(QString name, QWidget *parent) :
    ScintillaEdit(parent),
    name(name),
    indicatorResources(INDICATOR_MAX + 1)
{
    // Per the scintilla documentation, some parts of the range are not generally available
    indicatorResources.disableRange(0, 7);
    indicatorResources.disableRange(INDICATOR_IME, INDICATOR_IME_MAX);
    indicatorResources.disableRange(INDICATOR_HISTORY_REVERTED_TO_ORIGIN_INSERTION, INDICATOR_HISTORY_REVERTED_TO_MODIFIED_DELETION);
}

ScintillaNext::~ScintillaNext()
{
}

ScintillaNext *ScintillaNext::fromFile(const QString &filePath, bool tryToCreate)
{
    QFile file(filePath);
    ScintillaNext *editor = new ScintillaNext(file.fileName());

    if(tryToCreate && !file.exists()) {
        qInfo("Trying to create %s", qUtf8Printable(filePath));
        QDir d;
        d.mkpath(QFileInfo(file).path());

        QFile f(filePath);
        f.open(QIODevice::WriteOnly);
        f.close();
    }

    bool readSuccessful = editor->readFromDisk(file);

    if (!readSuccessful) {
        delete editor;
        return Q_NULLPTR;
    }

    editor->setFileInfo(filePath);

    return editor;
}

QString ScintillaNext::eolModeToString(int eolMode)
{
    if (eolMode == SC_EOL_CRLF)
        return QStringLiteral("crlf");
    else if (eolMode == SC_EOL_CR)
        return QStringLiteral("cr");
    else if (eolMode == SC_EOL_LF)
        return QStringLiteral("lf");
    else
        return QString(); // unknown
}

int ScintillaNext::stringToEolMode(QString eolMode)
{
    if (eolMode == QStringLiteral("crlf"))
        return SC_EOL_CRLF;
    else if (eolMode == QStringLiteral("cr"))
        return SC_EOL_CR;
    else if (eolMode == QStringLiteral("lf"))
        return SC_EOL_LF;
    else
        return -1;
}

int ScintillaNext::allocateIndicator(const QString &name)
{
    return indicatorResources.requestResource(name);
}

void ScintillaNext::goToRange(const Sci_CharacterRange &range)
{
    qInfo(Q_FUNC_INFO);

    if (isRangeValid(range)) {
        // Lines can be folded so make sure they are visible
        ensureVisible(lineFromPosition(range.cpMin));
        ensureVisible(lineFromPosition(range.cpMax));

        setSelection(range.cpMax, range.cpMin);
        scrollRange(range.cpMax, range.cpMin);
    }
}

QByteArray ScintillaNext::eolString() const
{
    const int eol = eOLMode();

    if (eol == SC_EOL_LF) return QByteArrayLiteral("\n");
    else if (eol == SC_EOL_CRLF) return QByteArrayLiteral("\r\n");
    else return QByteArrayLiteral("\r");
}

bool ScintillaNext::lineIsEmpty(int line)
{
    return (lineEndPosition(line) - positionFromLine(line)) == 0;
}

void ScintillaNext::deleteLine(int line)
{
    deleteRange(positionFromLine(line), lineLength(line));
}

void ScintillaNext::cutAllowLine()
{
    if (selectionEmpty()) {
        copyAllowLine();
        lineDelete();
    }
    else {
        cut();
    }
}

void ScintillaNext::modifyFoldLevels(int level, int action)
{
    const int totalLines = lineCount();

    int line = 0;
    while (line < totalLines) {
        int foldFlags = foldLevel(line); // Even though its called fold level it contains several other flags
        bool isHeader = foldFlags & SC_FOLDLEVELHEADERFLAG;
        int actualLevel = (foldFlags & SC_FOLDLEVELNUMBERMASK) - SC_FOLDLEVELBASE;

        if (isHeader && actualLevel == level) {
            foldLine(line, action);
            line = lastChild(line, -1) + 1;
        }
        else {
            ++line;
        }
    }
}

void ScintillaNext::foldAllLevels(int level)
{
    modifyFoldLevels(level, SC_FOLDACTION_CONTRACT);
}

void ScintillaNext::unFoldAllLevels(int level)
{
    modifyFoldLevels(level, SC_FOLDACTION_EXPAND);
}

void ScintillaNext::deleteLeadingEmptyLines()
{
    while (lineCount() > 1 && lineIsEmpty(0)) {
        deleteLine(0);
    }
}

void ScintillaNext::deleteTrailingEmptyLines()
{
    const int docLength = length();
    int position = docLength;

    while (position > 0 && isNewlineCharacter(charAt(position - 1))) {
        position--;
    }

    deleteRange(position, docLength - position);
}

bool ScintillaNext::isSavedToDisk() const
{
    return !canSaveToDisk();
}

bool ScintillaNext::canSaveToDisk() const
{
    // The buffer can be saved if:
    // - It is marked as a temporary since as soon as it gets saved it is no longer a temporary buffer
    // - A modified file
    // - A missing file since as soon as it is saved it is no longer missing.
    return temporary ||
           (bufferType == ScintillaNext::New && (modify() || encodingDirty)) ||
           (bufferType == ScintillaNext::File && (modify() || encodingDirty)) ||
            (bufferType == ScintillaNext::FileMissing);
}

void ScintillaNext::setName(const QString &name)
{
    this->name = name;

    emit renamed();
}

bool ScintillaNext::isFile() const
{
    return bufferType == ScintillaNext::File || bufferType == ScintillaNext::FileMissing;
}

QFileInfo ScintillaNext::getFileInfo() const
{
    Q_ASSERT(isFile());

    return fileInfo;
}

QString ScintillaNext::getPath() const
{
    Q_ASSERT(isFile());

    return QDir::toNativeSeparators(fileInfo.canonicalPath());
}

QString ScintillaNext::getFilePath() const
{
    Q_ASSERT(isFile());

    return QDir::toNativeSeparators(fileInfo.canonicalFilePath());
}

void ScintillaNext::setFoldMarkers(const QString &type)
{
    QMap<QString, QList<int>> map{
        {"simple", {SC_MARK_MINUS, SC_MARK_PLUS, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY}},
        {"arrow",  {SC_MARK_ARROWDOWN, SC_MARK_ARROW, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY}},
        {"circle", {SC_MARK_CIRCLEMINUS, SC_MARK_CIRCLEPLUS, SC_MARK_VLINE, SC_MARK_LCORNERCURVE, SC_MARK_CIRCLEPLUSCONNECTED, SC_MARK_CIRCLEMINUSCONNECTED, SC_MARK_TCORNERCURVE }},
        {"box",    {SC_MARK_BOXMINUS, SC_MARK_BOXPLUS, SC_MARK_VLINE, SC_MARK_LCORNER, SC_MARK_BOXPLUSCONNECTED, SC_MARK_BOXMINUSCONNECTED, SC_MARK_TCORNER }},
    };

    if (!map.contains(type))
        return;

    const auto types = map[type];
    markerDefine(SC_MARKNUM_FOLDEROPEN, types[0]);
    markerDefine(SC_MARKNUM_FOLDER, types[1]);
    markerDefine(SC_MARKNUM_FOLDERSUB, types[2]);
    markerDefine(SC_MARKNUM_FOLDERTAIL, types[3]);
    markerDefine(SC_MARKNUM_FOLDEREND, types[4]);
    markerDefine(SC_MARKNUM_FOLDEROPENMID, types[5]);
    markerDefine(SC_MARKNUM_FOLDERMIDTAIL, types[6]);
}

void ScintillaNext::close()
{
    emit closed();

    deleteLater();
}

QFileDevice::FileError ScintillaNext::save()
{
    qInfo(Q_FUNC_INFO);

    Q_ASSERT(isFile());

    emit aboutToSave();

    const QByteArray utf8Data = QByteArray::fromRawData((char*)characterPointer(), textLength());
    const QByteArray encoded = encodeFromText(utf8Data, currentEncoding);
    const QString path = fileInfo.filePath();
    QFileDevice::FileError writeSuccessful = writeToDisk(encoded, path);

    if (writeSuccessful == QFileDevice::NoError) {
        diskData = encoded;
        encodingDirty = false;
        updateTimestamp();
        setSavePoint();

        // If this was a temporary file, make sure it is not any more
        setTemporary(false);

        emit saved();
    }

    return writeSuccessful;
}

void ScintillaNext::reload()
{
    Q_ASSERT(isFile());

    // Ensure the file still exists.
    if (!QFile::exists(fileInfo.canonicalFilePath())) {
        return;
    }

    const int line = firstVisibleLine();
    const int caret = selectionNCaret(mainSelection());
    const int anchor = selectionNAnchor(mainSelection());

    // Remove all the text
    {
        const QSignalBlocker blocker(this);
        setUndoCollection(false);
        emptyUndoBuffer();
        setText("");
        setUndoCollection(true);
    }

    // NOTE: if the read fails then the buffer will be completely empty...which probably
    // isn't a good thing, but this should be a rare occurrence.
    QFile f(fileInfo.canonicalFilePath());
    bool readSuccessful = readFromDisk(f);

    if (!readSuccessful) {
        return;
    }

    updateTimestamp();
    setSavePoint();

    // If this was a temporary file, make sure it is not any more
    if (isTemporary())
        setTemporary(false);

    scrollVertical(line, 0);
    setSelection(caret, anchor);

    emit reloaded();
}

void ScintillaNext::convertTo(ScintillaNext::Encoding encoding)
{
    if (encoding == currentEncoding) {
        return;
    }

    // The in-memory document stays UTF-8; the new encoding only takes effect on
    // disk, so make sure the buffer is considered in need of saving.
    currentEncoding = encoding;
    encodingDirty = true;

    emit encodingChanged();
}

void ScintillaNext::openWith(ScintillaNext::Encoding encoding)
{
    // Like Notepad++'s "Open in encoding": re-interpret the source bytes as the
    // given encoding. When nothing is edited yet the real bytes on disk are used,
    // otherwise the current text is re-encoded with its own encoding first.
    const bool fromDisk = isFile() && !modify() && !encodingDirty;

    QByteArray source;
    if (fromDisk) {
        source = diskData;
    }
    else {
        const QByteArray utf8 = QByteArray::fromRawData((char*)characterPointer(), textLength());
        source = encodeFromText(utf8, currentEncoding);
    }

    const QByteArray newUtf8 = decodeToText(source, encoding).toUtf8();

    if (encoding != currentEncoding) {
        currentEncoding = encoding;
        emit encodingChanged();
    }

    const QByteArray utf8 = QByteArray::fromRawData((char*)characterPointer(), textLength());
    if (newUtf8 != utf8) {
        setTargetRange(0, textLength());
        replaceTarget(newUtf8.size(), newUtf8.constData());
        setSelection(0, 0);
    }

    if (fromDisk) {
        // A pure re-interpretation of the unchanged file: same state as having
        // opened the file with this encoding in the first place, so it is clean.
        encodingDirty = false;
        setSavePoint();
    }
    else {
        encodingDirty = true;
    }
}

void ScintillaNext::omitModifications()
{
    // If file modifications will be omitted just update file timestamp
    // so pop-up will be displayed only once per file modifications.
    updateTimestamp();
    setTemporary(true);

    return;
}

QFileDevice::FileError ScintillaNext::saveAs(const QString &newFilePath)
{
    bool isRenamed = bufferType == ScintillaNext::New || fileInfo.canonicalFilePath() != newFilePath;

    emit aboutToSave();

    const QByteArray utf8Data = QByteArray::fromRawData((char*)characterPointer(), textLength());
    const QByteArray encoded = encodeFromText(utf8Data, currentEncoding);
    QFileDevice::FileError saveSuccessful = writeToDisk(encoded, newFilePath);

    if (saveSuccessful == QFileDevice::NoError) {
        diskData = encoded;
        encodingDirty = false;
        setFileInfo(newFilePath);
        setSavePoint();

        // If this was a temporary file, make sure it is not any more
        setTemporary(false);

        emit saved();

        if (isRenamed) {
            emit renamed();
        }
    }

    return saveSuccessful;
}

QFileDevice::FileError ScintillaNext::saveCopyAs(const QString &filePath)
{
    const QByteArray utf8Data = QByteArray::fromRawData((char*)characterPointer(), textLength());
    return writeToDisk(encodeFromText(utf8Data, currentEncoding), filePath);
}

bool ScintillaNext::rename(const QString &newFilePath)
{
    emit aboutToSave();

    // Write out the buffer to the new path
    const QByteArray utf8Data = QByteArray::fromRawData((char*)characterPointer(), textLength());
    const QByteArray encoded = encodeFromText(utf8Data, currentEncoding);
    if (writeToDisk(encoded, newFilePath) == QFileDevice::NoError) {
        // Remove the old file
        const QString oldPath = fileInfo.canonicalFilePath();
        QFile::remove(oldPath);

        // Everything worked fine, so update the buffer's info
        diskData = encoded;
        encodingDirty = false;
        setFileInfo(newFilePath);
        setSavePoint();

        // If this was a temporary file, make sure it is not any more
        setTemporary(false);

        emit saved();

        emit renamed();

        return true;
    }

    return false;
}

ScintillaNext::FileStateChange ScintillaNext::checkFileForStateChange()
{
    if (bufferType == BufferType::New) {
        return FileStateChange::NoChange;
    }
    else if (bufferType == BufferType::File) {
        // refresh else exists() fails to notice missing file
        fileInfo.refresh();

        if (!fileInfo.exists()) {
            bufferType = BufferType::FileMissing;

            emit savePointChanged(false);

            return FileStateChange::Deleted;
        }

        // See if the timestamp changed
        if (modifiedTime != fileTimestamp()) {
            return FileStateChange::Modified;
        }
        else {
            return FileStateChange::NoChange;
        }
    }
    else if (bufferType == BufferType::FileMissing) {
        // See if it reappeared
        fileInfo.refresh();

        if (fileInfo.exists()) {
            bufferType = BufferType::File;

            return FileStateChange::Restored;
        }
        else {
            return FileStateChange::NoChange;
        }
    }

    qInfo("type() = %d", bufferType);
    Q_ASSERT(false);

    return FileStateChange::NoChange;
}

bool ScintillaNext::moveToTrash()
{
    if (QFile::exists(fileInfo.canonicalFilePath())) {
        QFile f(fileInfo.canonicalFilePath());

        return f.moveToTrash();
    }

    return false;
}

void ScintillaNext::toggleCommentSelection()
{
    ScintillaCommenter sc(this);
    sc.toggleSelection();
}

void ScintillaNext::commentLineSelection()
{
    ScintillaCommenter sc(this);
    sc.commentSelection();
}

void ScintillaNext::uncommentLineSelection()
{
    ScintillaCommenter sc(this);
    sc.uncommentSelection();
}

void ScintillaNext::removeDuplicateLines()
{
    QByteArray data = QByteArray::fromRawData((char*) characterPointer(), textLength());
    const QByteArray delim = eolString();

    auto lines = ByteArrayUtils::split(data, delim);
    int originalLineCount = lines.length();
    ByteArrayUtils::removeDuplicates(lines);

    if (originalLineCount == lines.length()){
        return; // No lines were removed
    }

    QByteArray result = ByteArrayUtils::join(lines, delim);

    const UndoAction ua(this);
    setTargetRange(0, textLength());
    replaceTarget(result.length(), result.constData());
}

void ScintillaNext::removeConsecutiveDuplicateLines()
{
    QByteArray data = QByteArray::fromRawData((char*) characterPointer(), textLength());
    const QByteArray delim = eolString();

    auto lines = ByteArrayUtils::split(data, delim);
    int originalLineCount = lines.length();
    ByteArrayUtils::removeConsecutiveDuplicates(lines);
    QByteArray result = ByteArrayUtils::join(lines, delim);

    if (originalLineCount == lines.length()){
        return; // No lines were removed
    }

    const UndoAction ua(this);
    setTargetRange(0, textLength());
    replaceTarget(result.length(), result.constData());
}

void ScintillaNext::dragEnterEvent(QDragEnterEvent *event)
{
    // Ignore all drag and drop events with urls and let the main application handle it
    if (event->mimeData()->hasUrls()) {
        return;
    }

    ScintillaEdit::dragEnterEvent(event);
}

void ScintillaNext::dropEvent(QDropEvent *event)
{
    // Ignore all drag and drop events with urls and let the main application handle it
    if (event->mimeData()->hasUrls()) {
        return;
    }

    ScintillaEdit::dropEvent(event);
}

bool ScintillaNext::readFromDisk(QFile &file)
{
    if (!file.exists()) {
        qWarning("Cannot read \"%s\": doesn't exist", qUtf8Printable(file.fileName()));
        return false;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("QFile::open() failed when opening \"%s\" - error code %d: %s", qUtf8Printable(file.fileName()), file.error(), qUtf8Printable(file.errorString()));
        return false;
    }

    // TODO: figure out what to do if "size" is too big
    const qint64 fileSize = file.size();
    allocate(fileSize);

    // Turn off undo collection and block signals during loading
    setUndoCollection(false);
    blockSignals(true);
    // TODO disable notifications
    // modEventMask(SC_MOD_NONE)?

    const QByteArray data = file.readAll();
    file.close();

    if (file.error() != QFileDevice::NoError || data.size() < fileSize) {
        qWarning("Something bad happened when reading disk %d %s", file.error(), qUtf8Printable(file.errorString()));
        this->blockSignals(false);
        setUndoCollection(true);
        return false;
    }

    // Determine the encoding (BOM → strict UTF-8 → GB18030) and decode the whole file
    currentEncoding = detectEncoding(data);
    diskData = data;
    encodingDirty = false;

    const QByteArray utf8Data = decodeToText(data, currentEncoding).toUtf8();
    appendText(utf8Data.size(), utf8Data.constData());

    // Restore it back
    this->blockSignals(false);
    setUndoCollection(true);
    // modEventMask(SC_MODEVENTMASKALL)?

    if (status() != SC_STATUS_OK) {
        qWarning("something bad happened in document->add_data() %ld", status());
        return false;
    }

    if (!QFileInfo(file).isWritable()) {
        qInfo("Setting file as read-only");
        setReadOnly(true);
    }

    return true;
}

QDateTime ScintillaNext::fileTimestamp()
{
    Q_ASSERT(bufferType != ScintillaNext::New);

    fileInfo.refresh();
    qInfo("%s last modified %s", qUtf8Printable(fileInfo.fileName()), qUtf8Printable(fileInfo.lastModified().toString()));
    return fileInfo.lastModified();
}

void ScintillaNext::updateTimestamp()
{
    modifiedTime = fileTimestamp();
}

void ScintillaNext::setFileInfo(const QString &filePath)
{
    fileInfo.setFile(filePath);
    fileInfo.makeAbsolute();

    Q_ASSERT(fileInfo.exists());

    name = fileInfo.fileName();
    bufferType = ScintillaNext::File;

    updateTimestamp();
}

void ScintillaNext::detachFileInfo(const QString &newName)
{
    setName(newName);

    bufferType = ScintillaNext::New;
}

void ScintillaNext::setTemporary(bool temp)
{
    temporary = temp;

    // Fake this signal
    emit savePointChanged(temporary);
}
