/*
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Gregor B. Rosenauer & Claude
 */
#include "Mobi.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <map>

namespace Mobi {

static uint32_t
BE32(const unsigned char* p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}


static uint16_t
BE16(const unsigned char* p)
{
	return (uint16_t)((p[0] << 8) | p[1]);
}


bool
LooksLikeMobi(const unsigned char* header, size_t size)
{
	return size >= 68 && memcmp(header + 60, "BOOKMOBI", 8) == 0;
}


// ---- the text

// The PalmDOC compression: literals, copies of up to 8 bytes, pairs of a distance and a length, and a space with a letter.
static void
PalmDecompress(const unsigned char* in, size_t size, std::string* out)
{
	size_t i = 0;
	while (i < size) {
		unsigned c = in[i++];
		if (c >= 1 && c <= 8) {
			for (unsigned k = 0; k < c && i < size; k++)
				out->push_back((char)in[i++]);
		} else if (c < 0x80) {
			out->push_back((char)c);
		} else if (c >= 0xC0) {
			out->push_back(' ');
			out->push_back((char)(c ^ 0x80));
		} else {
			if (i >= size)
				break;
			unsigned pair = (c << 8) | in[i++];
			unsigned distance = (pair >> 3) & 0x7FF;
			unsigned length = (pair & 7) + 3;
			if (distance == 0 || distance > out->size())
				break;
			for (unsigned k = 0; k < length; k++)
				out->push_back((*out)[out->size() - distance]);
		}
	}
}


// the size of one entry of the trailing data of a record, read from its end (a number of 7 bits per byte, the last byte has the top bit)
static size_t
TrailingEntrySize(const unsigned char* data, size_t size)
{
	size_t result = 0;
	int shift = 0;
	while (size > 0) {
		unsigned char v = data[size - 1];
		result |= (size_t)(v & 0x7F) << shift;
		shift += 7;
		size--;
		if ((v & 0x80) != 0 || shift >= 28)
			break;
	}
	return result;
}


// how many bytes at the end of a record are not text: the entries that the flags of the header name, and the overlap of a character
static size_t
TrailingBytes(const unsigned char* data, size_t size, unsigned flags)
{
	size_t total = 0;
	unsigned rest = flags >> 1;
	while (rest != 0) {
		if ((rest & 1) != 0 && size > total)
			total += TrailingEntrySize(data, size - total);
		rest >>= 1;
	}
	if ((flags & 1) != 0 && size > total)
		total += (data[size - total - 1] & 3) + 1;
	return total > size ? size : total;
}


// the code page 1252 in UTF-8 (what the text of old books is in)
static std::string
FromCp1252(const std::string& in)
{
	static const unsigned short high[32] = {
		0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
		0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };
	std::string out;
	out.reserve(in.size() + in.size() / 8);
	for (size_t i = 0; i < in.size(); i++) {
		unsigned char c = (unsigned char)in[i];
		unsigned code = c;
		if (c >= 0x80 && c < 0xA0)
			code = high[c - 0x80];
		if (code < 0x80)
			out.push_back((char)code);
		else if (code < 0x800) {
			out.push_back((char)(0xC0 | (code >> 6)));
			out.push_back((char)(0x80 | (code & 0x3F)));
		} else {
			out.push_back((char)(0xE0 | (code >> 12)));
			out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
			out.push_back((char)(0x80 | (code & 0x3F)));
		}
	}
	return out;
}


// ---- small helpers for text

static std::string
Lower(const std::string& s)
{
	std::string r(s);
	for (size_t i = 0; i < r.size(); i++)
		r[i] = (char)tolower((unsigned char)r[i]);
	return r;
}


static std::string
Escape(const std::string& s)
{
	std::string r;
	for (size_t i = 0; i < s.size(); i++) {
		switch (s[i]) {
			case '&': r += "&amp;"; break;
			case '<': r += "&lt;"; break;
			case '>': r += "&gt;"; break;
			case '"': r += "&quot;"; break;
			default: r.push_back(s[i]);
		}
	}
	return r;
}


static std::string
Number(unsigned long n, int digits = 0)
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%0*lu", digits, n);
	return buffer;
}


// the value of an attribute in a tag (quoted or not), found by its name; npos if it is not there
static bool
FindAttribute(const std::string& tag, const char* name, size_t* start, size_t* end)
{
	std::string lower = Lower(tag);
	size_t length = strlen(name);
	size_t at = 0;
	while ((at = lower.find(name, at)) != std::string::npos) {
		bool boundary = at > 0 && (isspace((unsigned char)lower[at - 1]) || lower[at - 1] == '"' || lower[at - 1] == '\'');
		size_t equal = at + length;
		while (equal < lower.size() && isspace((unsigned char)lower[equal]))
			equal++;
		if (boundary && equal < lower.size() && lower[equal] == '=') {
			size_t value = equal + 1;
			while (value < lower.size() && isspace((unsigned char)lower[value]))
				value++;
			if (value < tag.size() && (tag[value] == '"' || tag[value] == '\'')) {
				size_t close = tag.find(tag[value], value + 1);
				if (close == std::string::npos)
					return false;
				*start = value + 1;
				*end = close;
			} else {
				size_t close = value;
				while (close < tag.size() && !isspace((unsigned char)tag[close]) && tag[close] != '>' && tag[close] != '/')
					close++;
				*start = value;
				*end = close;
			}
			return true;
		}
		at += length;
	}
	return false;
}


// the whole attribute (name="value" with the space before it), removed from a tag
static void
RemoveAttribute(std::string* tag, const char* name)
{
	size_t start, end;
	if (!FindAttribute(*tag, name, &start, &end))
		return;
	size_t from = start;
	while (from > 0 && (*tag)[from - 1] != ' ' && !isspace((unsigned char)(*tag)[from - 1]))
		from--;
	while (from > 0 && isspace((unsigned char)(*tag)[from - 1]))
		from--;
	size_t to = end;
	if (to < tag->size() && ((*tag)[to] == '"' || (*tag)[to] == '\''))
		to++;
	tag->erase(from, to - from);
}


// ---- well-formed XHTML from the tag soup of old MOBI books

static bool
IsVoidElement(const std::string& name)
{
	static const char* kVoid[] = { "br", "hr", "img", "meta", "link", "input", "col", "area", "base", "param", "wbr", NULL };
	for (int i = 0; kVoid[i] != NULL; i++)
		if (name == kVoid[i])
			return true;
	return false;
}


// text with the "&" that is no entity escaped, and without the characters that XML does not allow
static std::string
CleanText(const std::string& in)
{
	std::string out;
	out.reserve(in.size());
	for (size_t i = 0; i < in.size(); i++) {
		unsigned char ch = (unsigned char)in[i];
		if (ch < 0x20 && ch != '\t' && ch != '\n' && ch != '\r')
			continue;
		if (ch == '&') {
			size_t k = i + 1;
			if (k < in.size() && in[k] == '#')
				k++;
			size_t first = k;
			while (k < in.size() && k - first < 12 && isalnum((unsigned char)in[k]))
				k++;
			if (k == first || k >= in.size() || in[k] != ';') {
				out += "&amp;";
				continue;
			}
			// XML knows five names only; the others of HTML become numbers
			std::string entity = in.substr(first, k - first);
			if (in[i + 1] != '#' && entity != "amp" && entity != "lt" && entity != "gt" && entity != "quot" && entity != "apos") {
				static const struct { const char* name; int code; } kNames[] = {
					{ "nbsp", 160 }, { "iexcl", 161 }, { "cent", 162 }, { "pound", 163 }, { "yen", 165 }, { "sect", 167 },
					{ "copy", 169 }, { "laquo", 171 }, { "reg", 174 }, { "deg", 176 }, { "plusmn", 177 }, { "micro", 181 },
					{ "para", 182 }, { "middot", 183 }, { "raquo", 187 }, { "frac14", 188 }, { "frac12", 189 }, { "frac34", 190 },
					{ "iquest", 191 }, { "times", 215 }, { "divide", 247 }, { "shy", 173 }, { "ensp", 8194 }, { "emsp", 8195 },
					{ "thinsp", 8201 }, { "ndash", 8211 }, { "mdash", 8212 }, { "lsquo", 8216 }, { "rsquo", 8217 }, { "sbquo", 8218 },
					{ "ldquo", 8220 }, { "rdquo", 8221 }, { "bdquo", 8222 }, { "dagger", 8224 }, { "bull", 8226 }, { "hellip", 8230 },
					{ "euro", 8364 }, { "trade", 8482 }, { "larr", 8592 }, { "rarr", 8594 }, { "minus", 8722 }, { "ne", 8800 },
					{ "le", 8804 }, { "ge", 8805 }, { NULL, 0 } };
				int code = 0;
				for (int n = 0; kNames[n].name != NULL && code == 0; n++)
					if (entity == kNames[n].name)
						code = kNames[n].code;
				if (code != 0) {
					out += "&#" + Number(code) + ";";
					i = k;
					continue;
				}
				out += "&amp;";
				continue;
			}
		}
		out.push_back((char)ch);
	}
	return out;
}


// an opening tag with lower case names, all values in quotes, no attribute twice, void elements closed
static std::string
NormalizeTag(const std::string& tag, const std::string& name)
{
	std::string out = "<" + name;
	std::vector<std::string> seen;
	size_t i = 1;
	while (i < tag.size() && !isspace((unsigned char)tag[i]) && tag[i] != '>' && tag[i] != '/')
		i++;
	while (i < tag.size()) {
		while (i < tag.size() && (isspace((unsigned char)tag[i]) || tag[i] == '/'))
			i++;
		if (i >= tag.size() || tag[i] == '>')
			break;
		size_t first = i;
		while (i < tag.size() && !isspace((unsigned char)tag[i]) && tag[i] != '=' && tag[i] != '>' && tag[i] != '/')
			i++;
		std::string attribute = Lower(tag.substr(first, i - first));
		while (i < tag.size() && isspace((unsigned char)tag[i]))
			i++;
		std::string value = attribute;
		if (i < tag.size() && tag[i] == '=') {
			i++;
			while (i < tag.size() && isspace((unsigned char)tag[i]))
				i++;
			if (i < tag.size() && (tag[i] == '"' || tag[i] == '\'')) {
				size_t close = tag.find(tag[i], i + 1);
				if (close == std::string::npos)
					close = tag.size() - 1;
				value = tag.substr(i + 1, close - i - 1);
				i = close + 1;
			} else {
				size_t from = i;
				while (i < tag.size() && !isspace((unsigned char)tag[i]) && tag[i] != '>')
					i++;
				value = tag.substr(from, i - from);
				if (!value.empty() && value[value.size() - 1] == '/')
					value.erase(value.size() - 1);
			}
			value = CleanText(value);
			std::string quoted;
			for (size_t k = 0; k < value.size(); k++) {
				if (value[k] == '"')
					quoted += "&quot;";
				else if (value[k] == '<')
					quoted += "&lt;";
				else
					quoted.push_back(value[k]);
			}
			value = quoted;
		}
		bool valid = !attribute.empty() && (isalpha((unsigned char)attribute[0]) || attribute[0] == '_');
		for (size_t k = 0; valid && k < attribute.size(); k++)
			valid = isalnum((unsigned char)attribute[k]) || attribute[k] == '-' || attribute[k] == '_' || attribute[k] == '.';
		if (!valid || std::find(seen.begin(), seen.end(), attribute) != seen.end())
			continue;
		seen.push_back(attribute);
		out += " " + attribute + "=\"" + value + "\"";
	}
	out += IsVoidElement(name) ? " />" : ">";
	return out;
}


// the elements that are open in a chapter: tags that close nothing are dropped, tags that were forgotten are added
struct OpenElements {
	std::vector<std::string> stack;

	std::string Open(const std::string& tag, const std::string& name)
	{
		std::string out;
		// elements that HTML closes by themselves: a new cell closes the open cell, a new row the open cell and row
		const char* closes[3] = { NULL, NULL, NULL };
		if (name == "p" || name == "li" || name == "option")
			closes[0] = name.c_str();
		else if (name == "dt" || name == "dd") {
			closes[0] = "dt";
			closes[1] = "dd";
		}
		if (name == "td" || name == "th") {
			closes[0] = "td";
			closes[1] = "th";
		} else if (name == "tr") {
			closes[0] = "td";
			closes[1] = "th";
			closes[2] = "tr";
		}
		bool closed = true;
		while (closed && !stack.empty()) {
			closed = false;
			for (int i = 0; i < 3; i++)
				if (closes[i] != NULL && stack.back() == closes[i]) {
					out += "</" + stack.back() + ">";
					stack.pop_back();
					closed = true;
					break;
				}
		}
		// text styles between the parts of a table have no place there (a browser would move them out): they are left out
		static const char* kTable[] = { "table", "thead", "tbody", "tfoot", "tr", NULL };
		bool inTable = false;
		size_t top = stack.size();
		while (top > 0 && stack[top - 1][0] == '!')
			top--;
		for (int i = 0; kTable[i] != NULL && top > 0; i++)
			inTable = inTable || stack[top - 1] == kTable[i];
		if (inTable && (name == "font" || name == "b" || name == "i" || name == "span" || name == "center" || name == "u")) {
			stack.push_back("!" + name);
			return out;
		}
		out += NormalizeTag(tag, name);
		if (!IsVoidElement(name))
			stack.push_back(name);
		return out;
	}

	std::string Close(const std::string& name)
	{
		if (IsVoidElement(name))
			return "";
		size_t at = stack.size();
		while (at > 0 && stack[at - 1] != name && stack[at - 1] != "!" + name)
			at--;
		if (at == 0)
			return "";
		std::string out;
		while (stack.size() >= at) {
			if (stack.back()[0] != '!')
				out += "</" + stack.back() + ">";
			stack.pop_back();
		}
		return out;
	}

	std::string CloseAll()
	{
		std::string out;
		while (!stack.empty()) {
			if (stack.back()[0] != '!')
				out += "</" + stack.back() + ">";
			stack.pop_back();
		}
		return out;
	}
};



static bool
IsImage(const unsigned char* data, size_t size, std::string* type, const char** extension)
{
	if (size > 3 && data[0] == 0xFF && data[1] == 0xD8) {
		*type = "image/jpeg";
		*extension = "jpg";
		return true;
	}
	if (size > 8 && memcmp(data, "\x89PNG", 4) == 0) {
		*type = "image/png";
		*extension = "png";
		return true;
	}
	if (size > 6 && memcmp(data, "GIF8", 4) == 0) {
		*type = "image/gif";
		*extension = "gif";
		return true;
	}
	return false;
}


static const char*
LanguageOfLocale(unsigned locale)
{
	switch (locale & 0xFF) {
		case 0x07: return "de";
		case 0x09: return "en";
		case 0x0A: return "es";
		case 0x0B: return "fi";
		case 0x0C: return "fr";
		case 0x10: return "it";
		case 0x11: return "ja";
		case 0x13: return "nl";
		case 0x14: return "no";
		case 0x15: return "pl";
		case 0x16: return "pt";
		case 0x1D: return "sv";
		case 0x19: return "ru";
		default: return "";
	}
}



// ---- reading

struct Reader {
	const unsigned char* data;
	size_t size;
	std::vector<size_t> offsets;	// of the records, the end of the last one

	const unsigned char* RecordData(size_t i) const { return data + offsets[i]; }
	size_t RecordSize(size_t i) const { return offsets[i + 1] - offsets[i]; }
	size_t Count() const { return offsets.size() - 1; }
};


static bool
ReadRecords(const unsigned char* data, size_t size, Reader* reader)
{
	reader->data = data;
	reader->size = size;
	if (size < 78 + 8)
		return false;
	size_t count = BE16(data + 76);
	if (count < 2 || 78 + count * 8 > size)
		return false;
	for (size_t i = 0; i < count; i++) {
		size_t offset = BE32(data + 78 + i * 8);
		if (offset > size || (i > 0 && offset < reader->offsets.back()))
			return false;
		reader->offsets.push_back(offset);
	}
	reader->offsets.push_back(size);
	return true;
}


// ---- the table of contents (the NCX index of the book: records of INDX with tags that are numbers of a variable width)

static bool
ReadVwi(const unsigned char* data, size_t size, size_t* at, size_t* value)
{
	// 7 bits at a time, the last byte has the top bit set
	size_t v = 0;
	while (*at < size) {
		unsigned char b = data[(*at)++];
		v = (v << 7) | (b & 0x7F);
		if ((b & 0x80) != 0) {
			*value = v;
			return true;
		}
	}
	return false;
}


static int
BitsSet(unsigned mask)
{
	int n = 0;
	for (; mask != 0; mask >>= 1)
		n += mask & 1;
	return n;
}


struct IndexEntry {
	std::string key;
	std::map<unsigned, std::vector<size_t> > tags;
};


// the entries of an index (the INDX records from a record on, with the strings of their CNCX records)
static void
ReadIndex(const Reader& reader, size_t index, std::vector<IndexEntry>* entries, std::vector<std::string>* cncx)
{
	if (index == 0xFFFFFFFFu || index + 1 >= reader.Count())
		return;
	const unsigned char* head = reader.RecordData(index);
	size_t headSize = reader.RecordSize(index);
	if (headSize < 60 || memcmp(head, "INDX", 4) != 0)
		return;
	size_t headLen = BE32(head + 4);
	size_t dataRecords = BE32(head + 24);
	size_t cncxRecords = BE32(head + 52);
	if (headLen + 12 > headSize || memcmp(head + headLen, "TAGX", 4) != 0)
		return;
	const unsigned char* tagx = head + headLen;
	size_t firstEntry = BE32(tagx + 4);
	size_t controlBytes = BE32(tagx + 8);
	if (headLen + firstEntry > headSize || firstEntry < 12)
		return;
	struct Tag { unsigned tag, values, mask, end; };
	std::vector<Tag> tags;
	for (size_t at = 12; at + 4 <= firstEntry; at += 4) {
		Tag t = { tagx[at], tagx[at + 1], tagx[at + 2], tagx[at + 3] };
		tags.push_back(t);
	}
	// the strings of the labels
	for (size_t i = 0; i < cncxRecords; i++) {
		size_t record = index + 1 + dataRecords + i;
		if (record >= reader.Count())
			break;
		cncx->push_back(std::string((const char*)reader.RecordData(record), reader.RecordSize(record)));
	}
	for (size_t r = 0; r < dataRecords; r++) {
		size_t record = index + 1 + r;
		if (record >= reader.Count())
			break;
		const unsigned char* data = reader.RecordData(record);
		size_t size = reader.RecordSize(record);
		if (size < 32 || memcmp(data, "INDX", 4) != 0)
			continue;
		size_t idxt = BE32(data + 20);
		size_t count = BE32(data + 24);
		if (idxt + 4 + count * 2 > size || memcmp(data + idxt, "IDXT", 4) != 0)
			continue;
		for (size_t e = 0; e < count; e++) {
			size_t start = BE16(data + idxt + 4 + e * 2);
			size_t end = e + 1 < count ? BE16(data + idxt + 4 + (e + 1) * 2) : idxt;
			if (start >= size || end > size || start >= end)
				continue;
			size_t keyLength = data[start];
			size_t control = start + 1 + keyLength;
			size_t at = control + controlBytes;
			if (at > end)
				continue;
			IndexEntry entry;
			entry.key.assign((const char*)data + start + 1, keyLength);
			// the values of each tag, as the control bytes say how many there are
			size_t controlIndex = 0;
			struct Pending { unsigned tag, valuesPerEntry; size_t count, bytes; bool byBytes; };
			std::vector<Pending> pending;
			for (size_t t = 0; t < tags.size(); t++) {
				if (tags[t].end == 1) {
					controlIndex++;
					continue;
				}
				if (control + controlIndex >= end)
					break;
				unsigned value = data[control + controlIndex] & tags[t].mask;
				if (value == 0)
					continue;
				Pending p = { tags[t].tag, tags[t].values, 0, 0, false };
				if (value == tags[t].mask) {
					if (BitsSet(tags[t].mask) > 1) {
						size_t bytes;
						if (!ReadVwi(data, end, &at, &bytes))
							break;
						p.bytes = bytes;
						p.byBytes = true;
					} else
						p.count = 1;
				} else {
					unsigned mask = tags[t].mask;
					while ((mask & 1) == 0) {
						mask >>= 1;
						value >>= 1;
					}
					p.count = value;
				}
				pending.push_back(p);
			}
			for (size_t t = 0; t < pending.size(); t++) {
				if (!pending[t].byBytes) {
					for (size_t k = 0; k < pending[t].count * pending[t].valuesPerEntry; k++) {
						size_t v;
						if (!ReadVwi(data, end, &at, &v))
							break;
						entry.tags[pending[t].tag].push_back(v);
					}
				} else {
					size_t until = at + pending[t].bytes;
					while (at < until) {
						size_t v;
						if (!ReadVwi(data, end, &at, &v))
							break;
						entry.tags[pending[t].tag].push_back(v);
					}
				}
			}
			entries->push_back(entry);
		}
	}
}


// a string of the CNCX records: a length (7 bits per byte, the last byte has the top bit) and the bytes
static std::string
CncxString(const std::vector<std::string>& cncx, size_t offset)
{
	size_t record = offset >> 16, at = offset & 0xFFFF;
	if (record >= cncx.size() || at >= cncx[record].size())
		return std::string();
	const std::string& s = cncx[record];
	size_t length, p = at;
	if (!ReadVwi((const unsigned char*)s.data(), s.size(), &p, &length))
		return std::string();
	return s.substr(p, std::min(length, s.size() - p));
}


struct NcxEntry {
	size_t offset;		// in the text (the old format)
	size_t label;
	int    depth;
	size_t fid, fidOffset;	// the fragment and the place in it (the new format)
	bool   hasFid;
};


static void
ReadNcx(const Reader& reader, const unsigned char* rec0, size_t rec0Size, size_t headerLength,
	std::vector<NcxEntry>* entries, std::vector<std::string>* cncx)
{
	if (16 + headerLength < 0xF8 || rec0Size < 0xF8)
		return;
	std::vector<IndexEntry> index;
	ReadIndex(reader, BE32(rec0 + 0xF4), &index, cncx);
	for (size_t i = 0; i < index.size(); i++) {
		std::map<unsigned, std::vector<size_t> >& tags = index[i].tags;
		if (tags[3].empty())
			continue;
		NcxEntry entry;
		entry.offset = tags[1].empty() ? 0 : tags[1][0];
		entry.label = tags[3][0];
		entry.depth = tags[4].empty() ? 0 : (int)tags[4][0];
		entry.hasFid = tags[6].size() >= 2;
		entry.fid = entry.hasFid ? tags[6][0] : 0;
		entry.fidOffset = entry.hasFid ? tags[6][1] : 0;
		if (tags[1].empty() && !entry.hasFid)
			continue;
		entries->push_back(entry);
	}
}


static std::string
NcxLabel(const std::vector<std::string>& cncx, size_t offset)
{
	return CncxString(cncx, offset);
}


// ---- the new format (KF8): HTML in parts that the skeleton and fragment indexes put together, flows of style sheets and
// SVG, links and resources with addresses of the form kindle:...

static size_t
Base32Value(const std::string& s)
{
	size_t v = 0;
	for (size_t i = 0; i < s.size(); i++) {
		char c = s[i];
		unsigned d = c >= '0' && c <= '9' ? c - '0' : (c >= 'A' && c <= 'V' ? c - 'A' + 10 : (c >= 'a' && c <= 'v' ? c - 'a' + 10 : 0));
		v = v * 32 + d;
	}
	return v;
}


// the aid of an element that a fragment belongs to, from the selector the index has for it: P-//*[@aid='0001']
static std::string
AidOfSelector(const std::string& selector)
{
	size_t at = selector.find("@aid=");
	if (at == std::string::npos || at + 6 >= selector.size())
		return std::string();
	char quote = selector[at + 5];
	size_t end = selector.find(quote, at + 6);
	if (end == std::string::npos)
		return std::string();
	return selector.substr(at + 6, end - at - 6);
}


struct Kf8 {
	std::vector<std::string> flows;
	std::vector<std::string> parts;
	std::vector<size_t> partOfFragment;
	std::vector<std::string> aidOfFragment;
	std::map<size_t, size_t> imageOfRecord;
	size_t firstImage;
	Book* book;
};


static std::string
PartName(size_t part)
{
	return "part" + Number(part + 1, 4) + ".xhtml";
}


// the addresses of the form kindle:... in a text become those of the files of the book
static std::string
Kf8Addresses(const Kf8& k, const std::string& in, std::vector<std::string>* flowNames)
{
	std::string out;
	size_t i = 0;
	while (i < in.size()) {
		size_t at = in.find("kindle:", i);
		if (at == std::string::npos) {
			out.append(in, i, std::string::npos);
			break;
		}
		out.append(in, i, at - i);
		size_t p = at + 7;
		size_t colon = in.find(':', p);
		std::string kind = colon == std::string::npos ? std::string() : in.substr(p, colon - p);
		std::string replacement;
		size_t end = at + 7;
		if (kind == "pos" && in.compare(colon, 5, ":fid:") == 0 && colon + 5 + 4 <= in.size()) {
			size_t fid = Base32Value(in.substr(colon + 5, 4));
			// (the place in the fragment is not used: the link goes to the element of the fragment)
			end = colon + 5 + 4;
			size_t off = in.find(":off:", end);
			if (off == end)
				end += 5 + 10;
			if (fid < k.partOfFragment.size()) {
				replacement = PartName(k.partOfFragment[fid]);
				if (!k.aidOfFragment[fid].empty())
					replacement += "#a-" + k.aidOfFragment[fid];
			}
		} else if (kind == "flow" || kind == "embed") {
			size_t n = colon != std::string::npos ? Base32Value(in.substr(colon + 1, 4)) : 0;
			end = colon != std::string::npos ? colon + 1 + 4 : at + 7;
			std::string mime;
			if (end < in.size() && in[end] == '?') {
				size_t stop = end;
				while (stop < in.size() && in[stop] != '"' && in[stop] != '\'' && in[stop] != ')' && !isspace((unsigned char)in[stop]))
					stop++;
				std::string query = in.substr(end + 1, stop - end - 1);
				size_t m = query.find("mime=");
				if (m != std::string::npos)
					mime = query.substr(m + 5);
				end = stop;
			}
			if (kind == "embed") {
				std::map<size_t, size_t>::const_iterator image = k.imageOfRecord.find(k.firstImage + n - 1);
				if (image != k.imageOfRecord.end())
					replacement = k.book->images[image->second].name;
			} else if (n < k.flows.size()) {
				replacement = "flow" + Number(n, 4) + (mime == "text/css" ? ".css" : (mime == "image/svg+xml" ? ".svg" : ".bin"));
				if (flowNames != NULL)
					flowNames->push_back(replacement);
			}
		} else
			end = at + 7;
		out += replacement;
		i = end;
	}
	return out;
}


// the aid attributes of the elements become the ids that the links need (a link goes to #a-<aid>)
static std::string
Kf8Anchors(const std::string& in)
{
	std::string out;
	size_t i = 0;
	bool inBody = false;
	while (i < in.size()) {
		size_t open = in.find('<', i);
		if (open == std::string::npos) {
			out.append(in, i, std::string::npos);
			break;
		}
		out.append(in, i, open - i);
		size_t close = open;
		char quote = 0;
		while (close < in.size()) {
			char c = in[close];
			if (quote != 0) {
				if (c == quote)
					quote = 0;
			} else if (c == '"' || c == '\'')
				quote = c;
			else if (c == '>')
				break;
			close++;
		}
		if (close >= in.size())
			close = in.size() - 1;
		std::string tag = in.substr(open, close - open + 1);
		size_t start, end;
		if (Lower(tag.substr(0, 5)) == "<body")
			inBody = true;
		if (open + 1 < in.size() && isalpha((unsigned char)in[open + 1]) && FindAttribute(tag, "aid", &start, &end)) {
			std::string aid = tag.substr(start, end - start);
			RemoveAttribute(&tag, "aid");
			size_t idStart, idEnd;
			if (!FindAttribute(tag, "id", &idStart, &idEnd)) {
				size_t nameEnd = 1;
				while (nameEnd < tag.size() && !isspace((unsigned char)tag[nameEnd]) && tag[nameEnd] != '>' && tag[nameEnd] != '/')
					nameEnd++;
				tag.insert(nameEnd, " id=\"a-" + aid + "\"");
			} else {
				// the id stays, the anchor goes before the element (in the body)
				if (inBody)
					out += "<a id=\"a-" + aid + "\"></a>";
			}
		}
		out += tag;
		i = close + 1;
	}
	return out;
}


static Result
ReadKf8(const Reader& reader, const unsigned char* rec0, size_t rec0Size, size_t headerLength, const std::string& text,
	size_t firstImage, const std::map<size_t, size_t>& imageOfRecord, Book* book)
{
	if (16 + headerLength < 0x108 || rec0Size < 0x108)
		return kUnsupported;
	Kf8 k;
	k.firstImage = firstImage;
	k.imageOfRecord = imageOfRecord;
	k.book = book;

	// the flows: the HTML of the book is the first, the style sheets and SVG images are the others
	size_t fdst = BE32(rec0 + 0xC0);
	if (fdst < reader.Count() && reader.RecordSize(fdst) >= 12 && memcmp(reader.RecordData(fdst), "FDST", 4) == 0) {
		const unsigned char* table = reader.RecordData(fdst);
		size_t tableOffset = BE32(table + 4), count = BE32(table + 8);
		for (size_t i = 0; i < count && tableOffset + (i + 1) * 8 <= reader.RecordSize(fdst); i++) {
			size_t from = BE32(table + tableOffset + i * 8), to = BE32(table + tableOffset + i * 8 + 4);
			if (from <= to && to <= text.size())
				k.flows.push_back(text.substr(from, to - from));
			else
				k.flows.push_back(std::string());
		}
	}
	if (k.flows.empty())
		k.flows.push_back(text);

	std::vector<IndexEntry> skeletons, fragments;
	std::vector<std::string> skeletonCncx, fragmentCncx;
	ReadIndex(reader, BE32(rec0 + 0xFC), &skeletons, &skeletonCncx);
	ReadIndex(reader, BE32(rec0 + 0xF8), &fragments, &fragmentCncx);
	if (skeletons.empty())
		return kUnsupported;

	// the parts: a skeleton with its fragments put in (the fragments follow the skeleton in the text)
	const std::string& main = k.flows[0];
	size_t fragmentIndex = 0;
	for (size_t i = 0; i < skeletons.size(); i++) {
		std::map<unsigned, std::vector<size_t> >& tags = skeletons[i].tags;
		if (tags[6].size() < 2)
			continue;
		size_t chunks = tags[1].empty() ? 0 : tags[1][0];
		size_t skeletonStart = tags[6][0], skeletonLength = tags[6][1];
		if (skeletonStart + skeletonLength > main.size())
			continue;
		size_t base = skeletonStart + skeletonLength;
		std::string part = main.substr(skeletonStart, skeletonLength);
		for (size_t c = 0; c < chunks && fragmentIndex < fragments.size(); c++, fragmentIndex++) {
			std::map<unsigned, std::vector<size_t> >& fragment = fragments[fragmentIndex].tags;
			size_t length = fragment[6].size() >= 2 ? fragment[6][1] : 0;
			if (base + length > main.size())
				length = main.size() > base ? main.size() - base : 0;
			long insert = atol(fragments[fragmentIndex].key.c_str()) - (long)skeletonStart;
			if (insert < 0 || (size_t)insert > part.size())
				insert = (long)part.size();
			// the place must not be inside a tag
			size_t lastOpen = part.rfind('<', insert > 0 ? (size_t)insert - 1 : 0);
			size_t lastClose = part.rfind('>', insert > 0 ? (size_t)insert - 1 : 0);
			if (insert > 0 && lastOpen != std::string::npos && (lastClose == std::string::npos || lastClose < lastOpen)) {
				size_t after = part.find('>', (size_t)insert);
				if (after != std::string::npos)
					insert = (long)after + 1;
			}
			part.insert((size_t)insert, main, base, length);
			base += length;
			k.partOfFragment.push_back(k.parts.size());
			k.aidOfFragment.push_back(fragment[2].empty() ? std::string() : AidOfSelector(CncxString(fragmentCncx, fragment[2][0])));
		}
		k.parts.push_back(part);
	}
	if (k.parts.empty())
		return kBroken;

	// the other flows become files; the addresses in all of them are those of the files of the book
	for (size_t i = 0; i < k.parts.size(); i++) {
		Chapter chapter;
		chapter.name = PartName(i);
		chapter.xhtml = Kf8Addresses(k, Kf8Anchors(k.parts[i]), NULL);
		book->chapters.push_back(chapter);
	}
	for (size_t i = 1; i < k.flows.size(); i++) {
		const std::string& flow = k.flows[i];
		if (flow.empty())
			continue;
		Image resource;
		bool svg = flow.find("<svg") != std::string::npos && flow.find("<svg") < 400;
		bool css = !svg && flow.find('{') != std::string::npos;
		resource.name = "flow" + Number(i, 4) + (svg ? ".svg" : (css ? ".css" : ".bin"));
		resource.type = svg ? "image/svg+xml" : (css ? "text/css" : "application/octet-stream");
		resource.data = Kf8Addresses(k, flow, NULL);
		book->images.push_back(resource);
	}

	// the table of contents: where the entries lead is a fragment
	std::vector<NcxEntry> ncxEntries;
	std::vector<std::string> cncx;
	ReadNcx(reader, rec0, rec0Size, headerLength, &ncxEntries, &cncx);
	for (size_t i = 0; i < ncxEntries.size(); i++) {
		if (!ncxEntries[i].hasFid || ncxEntries[i].fid >= k.partOfFragment.size())
			continue;
		TocEntry entry;
		entry.title = NcxLabel(cncx, ncxEntries[i].label);
		entry.level = ncxEntries[i].depth + 1;
		entry.href = PartName(k.partOfFragment[ncxEntries[i].fid]);
		if (!k.aidOfFragment[ncxEntries[i].fid].empty())
			entry.href += "#a-" + k.aidOfFragment[ncxEntries[i].fid];
		book->toc.push_back(entry);
	}
	return kOk;
}

Result
Read(const unsigned char* data, size_t size, Book* book)
{
	if (!LooksLikeMobi(data, size))
		return kNotMobi;
	Reader reader;
	if (!ReadRecords(data, size, &reader))
		return kBroken;
	const unsigned char* rec0 = reader.RecordData(0);
	size_t rec0Size = reader.RecordSize(0);
	if (rec0Size < 16)
		return kBroken;

	unsigned compression = BE16(rec0);
	unsigned textRecords = BE16(rec0 + 8);
	unsigned encryption = BE16(rec0 + 12);
	if (encryption != 0)
		return kEncrypted;
	if (compression != 1 && compression != 2)
		return kUnsupported;	// Huffman/CDIC

	unsigned encoding = 65001, version = 0, locale = 0, flags = 0;
	size_t headerLength = 0, firstImage = 0, fullNameOffset = 0, fullNameLength = 0;
	unsigned exthFlags = 0;
	if (rec0Size >= 132 && memcmp(rec0 + 16, "MOBI", 4) == 0) {
		headerLength = BE32(rec0 + 20);
		encoding = BE32(rec0 + 28);
		version = BE32(rec0 + 36);
		fullNameOffset = BE32(rec0 + 84);
		fullNameLength = BE32(rec0 + 88);
		locale = BE32(rec0 + 92);
		firstImage = BE32(rec0 + 108);
		exthFlags = BE32(rec0 + 128);
		if (headerLength >= 0xE4 && rec0Size >= 0xF4)
			flags = BE16(rec0 + 0xF2);
	}
	// the metadata: the title and the records of the EXTH header
	std::string pdbName((const char*)data, strnlen((const char*)data, 32));
	book->title = pdbName;
	for (size_t i = 0; i < book->title.size(); i++)
		if (book->title[i] == '_')
			book->title[i] = ' ';
	if (fullNameLength > 0 && fullNameOffset + fullNameLength <= rec0Size)
		book->title.assign((const char*)rec0 + fullNameOffset, fullNameLength);
	long coverOffset = -1;
	std::string updatedTitle;
	std::string language;
	if ((exthFlags & 0x40) != 0 && 16 + headerLength + 12 <= rec0Size
		&& memcmp(rec0 + 16 + headerLength, "EXTH", 4) == 0) {
		size_t at = 16 + headerLength + 12;
		unsigned count = BE32(rec0 + 16 + headerLength + 8);
		for (unsigned i = 0; i < count && at + 8 <= rec0Size; i++) {
			unsigned type = BE32(rec0 + at);
			size_t length = BE32(rec0 + at + 4);
			if (length < 8 || at + length > rec0Size)
				break;
			std::string value((const char*)rec0 + at + 8, length - 8);
			switch (type) {
				case 100: book->authors.push_back(value); break;
				case 101: book->publisher = value; break;
				case 103: book->description = value; break;
				case 104: book->isbn = value; break;
				case 105: book->subjects.push_back(value); break;
				case 106: book->date = value; break;
				case 109: book->rights = value; break;
				case 201:
					if (value.size() == 4)
						coverOffset = (long)BE32((const unsigned char*)value.data());
					break;
				case 503: updatedTitle = value; break;
				case 524: language = value; break;
			}
			at += length;
		}
	}
	if (!updatedTitle.empty())
		book->title = updatedTitle;
	book->language = !language.empty() ? language : LanguageOfLocale(locale);
	book->identifier = "mobi-" + Number(BE32(rec0 + 32 > rec0 + rec0Size ? rec0 : rec0 + 32 /* the unique id */));

	// the text: the records after the first
	std::string text;
	for (unsigned i = 1; i <= textRecords && i < reader.Count(); i++) {
		const unsigned char* record = reader.RecordData(i);
		size_t length = reader.RecordSize(i);
		length -= TrailingBytes(record, length, flags);
		if (compression == 2)
			PalmDecompress(record, length, &text);
		else
			text.append((const char*)record, length);
	}
	if (text.empty())
		return kBroken;

	// the images
	std::map<size_t, size_t> imageOfRecord;
	if (firstImage > 0) {
		for (size_t i = firstImage; i < reader.Count(); i++) {
			std::string type;
			const char* extension = "";
			if (!IsImage(reader.RecordData(i), reader.RecordSize(i), &type, &extension))
				continue;
			Image image;
			image.name = "image" + Number(book->images.size() + 1, 4) + "." + extension;
			image.type = type;
			image.data.assign((const char*)reader.RecordData(i), reader.RecordSize(i));
			imageOfRecord[i] = book->images.size();
			book->images.push_back(image);
		}
		if (coverOffset >= 0 && imageOfRecord.count(firstImage + coverOffset) > 0)
			book->coverImage = (int)imageOfRecord[firstImage + coverOffset];
	}

	if (version >= 8)
		return ReadKf8(reader, rec0, rec0Size, headerLength, text, firstImage, imageOfRecord, book);

	// the chapters: the text is cut where a page break is (the ranges in the bytes of the text)
	std::vector<std::pair<size_t, size_t> > ranges;
	{
		size_t start = 0;
		std::string lower = Lower(text);
		size_t at = 0;
		while ((at = lower.find("<mbp:pagebreak", at)) != std::string::npos) {
			size_t close = lower.find('>', at);
			if (close == std::string::npos)
				break;
			ranges.push_back(std::make_pair(start, at));
			start = close + 1;
			at = close + 1;
		}
		ranges.push_back(std::make_pair(start, text.size()));
	}
	// a range that has no text is dropped; what points into it goes to the next one
	std::vector<size_t> rangeOfChapter;
	std::vector<std::pair<size_t, size_t> > chapters;
	for (size_t i = 0; i < ranges.size(); i++) {
		bool hasText = false;
		for (size_t k = ranges[i].first; k < ranges[i].second && !hasText; k++)
			hasText = !isspace((unsigned char)text[k]);
		if (hasText || (i + 1 == ranges.size() && chapters.empty()))
			chapters.push_back(ranges[i]);
	}
	if (chapters.empty())
		return kBroken;
	// the chapter that a place in the text belongs to
	struct Locator {
		const std::vector<std::pair<size_t, size_t> >* chapters;
		size_t operator()(size_t offset) const {
			for (size_t i = 0; i < chapters->size(); i++)
				if (offset < (*chapters)[i].second)
					return i;
			return chapters->size() - 1;
		}
	} chapterAt = { &chapters };
	std::vector<std::string> chapterNames;
	for (size_t i = 0; i < chapters.size(); i++)
		chapterNames.push_back("chapter" + Number(i + 1, 3) + ".xhtml");

	// the table of contents of the book (NCX): where its entries lead
	std::vector<NcxEntry> ncxEntries;
	std::vector<std::string> cncx;
	ReadNcx(reader, rec0, rec0Size, headerLength, &ncxEntries, &cncx);

	// the places that the links point to (<a filepos=...>)
	std::vector<size_t> targets;
	for (size_t i = 0; i < ncxEntries.size(); i++)
		targets.push_back(ncxEntries[i].offset);
	{
		std::string lower = Lower(text);
		size_t at = 0;
		while ((at = lower.find("<a ", at)) != std::string::npos) {
			size_t close = lower.find('>', at);
			if (close == std::string::npos)
				break;
			std::string tag = text.substr(at, close - at + 1);
			size_t start, end;
			if (FindAttribute(tag, "filepos", &start, &end))
				targets.push_back((size_t)strtoul(tag.substr(start, end - start).c_str(), NULL, 10));
			at = close + 1;
		}
		std::sort(targets.begin(), targets.end());
		targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
	}

	// each chapter: the tags that mean nothing for a reader of EPUB are dropped, links and images get their addresses, the places
	// of the links their anchors; the headings make the table of contents
	size_t headingCount = 0;
	int openHeading = 0;
	size_t openEntry = 0;
	for (size_t c = 0; c < chapters.size(); c++) {
		std::string out;
		size_t i = chapters[c].first, end = chapters[c].second;
		size_t nextTarget = std::lower_bound(targets.begin(), targets.end(), i) - targets.begin();
		bool skipHead = false;
		OpenElements open;
		while (i < end) {
			if (text[i] == '<') {
				size_t close = text.find('>', i);
				if (close == std::string::npos || close >= end)
					close = end - 1;
				std::string tag = text.substr(i, close - i + 1);
				// anchors for the places in the range of the tag (before the tag if it starts there)
				std::string anchors;
				while (nextTarget < targets.size() && targets[nextTarget] <= close) {
					anchors += "<a id=\"fp" + Number(targets[nextTarget]) + "\"></a>";
					nextTarget++;
				}
				std::string lower = Lower(tag);
				std::string name;
				{
					size_t k = (lower.size() > 1 && lower[1] == '/') ? 2 : 1;
					while (k < lower.size() && (isalnum((unsigned char)lower[k]) || lower[k] == ':' || lower[k] == '!' || lower[k] == '?'))
						name.push_back(lower[k++]);
				}
				bool closing = lower.size() > 1 && lower[1] == '/';
				if (skipHead) {
					if (closing && name == "head")
						skipHead = false;
					out += anchors;
				} else if (name == "head" && !closing) {
					skipHead = true;
					out += anchors;
				} else if (name.compare(0, 4, "mbp:") == 0 || name == "html" || name == "body" || name == "!doctype" || name == "?xml"
						|| name == "meta") {
					out += anchors;
				} else {
					out += anchors;
					if (!closing && name == "a") {
						size_t start, valueEnd;
						if (FindAttribute(tag, "filepos", &start, &valueEnd)) {
							size_t target = (size_t)strtoul(tag.substr(start, valueEnd - start).c_str(), NULL, 10);
							RemoveAttribute(&tag, "filepos");
							tag.insert(2, " href=\"" + chapterNames[chapterAt(target)] + "#fp" + Number(target) + "\"");
						}
					} else if (!closing && name == "img") {
						size_t start, valueEnd;
						if (FindAttribute(tag, "recindex", &start, &valueEnd)) {
							size_t record = firstImage + strtoul(tag.substr(start, valueEnd - start).c_str(), NULL, 10) - 1;
							std::map<size_t, size_t>::const_iterator image = imageOfRecord.find(record);
							RemoveAttribute(&tag, "recindex");
							RemoveAttribute(&tag, "hirecindex");
							RemoveAttribute(&tag, "lorecindex");
							if (image != imageOfRecord.end())
								tag.insert(4, " src=\"" + book->images[image->second].name + "\"");
						}
						if (lower.find("alt=") == std::string::npos)
							tag.insert(4, " alt=\"\"");
					} else if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '3') {
						if (!closing) {
							openHeading = name[1] - '0';
							headingCount++;
							TocEntry entry;
							entry.level = openHeading;
							entry.href = chapterNames[c] + "#h" + Number(headingCount);
							book->toc.push_back(entry);
							openEntry = book->toc.size() - 1;
							out += "<a id=\"h" + Number(headingCount) + "\"></a>";
						} else
							openHeading = 0;
					}
					if (name.empty() || name[0] == '!' || name[0] == '?' || name.find(':') != std::string::npos) {
						// comments, instructions and tags of other namespaces mean nothing here
					} else if (closing)
						out += open.Close(name);
					else
						out += open.Open(tag, name);
				}
				i = close + 1;
			} else {
				size_t next = text.find('<', i);
				if (next == std::string::npos || next > end)
					next = end;
				std::string run;
				while (i < next) {
					size_t upto = next;
					if (nextTarget < targets.size() && targets[nextTarget] < next && targets[nextTarget] >= i)
						upto = targets[nextTarget];
					run.append(text, i, upto - i);
					i = upto;
					if (nextTarget < targets.size() && targets[nextTarget] == i) {
						run += "<a id=\"fp" + Number(targets[nextTarget]) + "\"></a>";
						nextTarget++;
					} else if (nextTarget < targets.size() && targets[nextTarget] < i)
						nextTarget++;
				}
				if (!skipHead) {
					out += CleanText(run);
					if (openHeading > 0) {
						// the words of the heading, without tags (they are text here)
						std::string& title = book->toc[openEntry].title;
						for (size_t k = 0; k < run.size(); k++)
							title.push_back(run[k] == '\n' || run[k] == '\r' ? ' ' : run[k]);
					}
				}
			}
		}
		out += open.CloseAll();
		if (encoding == 1252)
			out = FromCp1252(out);
		Chapter chapter;
		chapter.name = chapterNames[c];
		chapter.xhtml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<html xmlns=\"http://www.w3.org/1999/xhtml\">\n<head><title>"
			+ Escape(book->title) + "</title></head>\n<body>\n" + out + "\n</body>\n</html>\n";
		book->chapters.push_back(chapter);
	}
	if (encoding == 1252) {
		book->title = FromCp1252(book->title);
		for (size_t i = 0; i < book->authors.size(); i++)
			book->authors[i] = FromCp1252(book->authors[i]);
		book->publisher = FromCp1252(book->publisher);
		book->description = FromCp1252(book->description);
		for (size_t i = 0; i < book->subjects.size(); i++)
			book->subjects[i] = FromCp1252(book->subjects[i]);
		for (size_t i = 0; i < book->toc.size(); i++)
			book->toc[i].title = FromCp1252(book->toc[i].title);
	}
	// the table of contents of the book if it has one; else the headings
	if (!ncxEntries.empty()) {
		book->toc.clear();
		for (size_t i = 0; i < ncxEntries.size(); i++) {
			TocEntry entry;
			entry.title = NcxLabel(cncx, ncxEntries[i].label);
			if (encoding == 1252)
				entry.title = FromCp1252(entry.title);
			entry.level = ncxEntries[i].depth + 1;
			entry.href = chapterNames[chapterAt(ncxEntries[i].offset)] + "#fp" + Number(ncxEntries[i].offset);
			book->toc.push_back(entry);
		}
	}
	// the headings without words are no entries
	std::vector<TocEntry> toc;
	for (size_t i = 0; i < book->toc.size(); i++) {
		std::string& title = book->toc[i].title;
		size_t first = title.find_first_not_of(" \t\r\n");
		size_t last = title.find_last_not_of(" \t\r\n");
		if (first == std::string::npos)
			continue;
		title = title.substr(first, last - first + 1);
		toc.push_back(book->toc[i]);
	}
	book->toc.swap(toc);
	return kOk;
}


// ---- the EPUB

static uint32_t
Crc32(const std::string& data)
{
	static uint32_t table[256];
	static bool made = false;
	if (!made) {
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
		made = true;
	}
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < data.size(); i++)
		crc = table[(crc ^ (unsigned char)data[i]) & 0xFF] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFFu;
}


static void
Put16(std::string* out, unsigned v)
{
	out->push_back((char)(v & 0xFF));
	out->push_back((char)((v >> 8) & 0xFF));
}


static void
Put32(std::string* out, uint32_t v)
{
	Put16(out, v & 0xFFFF);
	Put16(out, v >> 16);
}


struct ZipEntry {
	std::string name;
	std::string data;
};


static std::string
MakeZip(const std::vector<ZipEntry>& entries)
{
	std::string out, directory;
	for (size_t i = 0; i < entries.size(); i++) {
		const ZipEntry& e = entries[i];
		uint32_t crc = Crc32(e.data);
		uint32_t offset = (uint32_t)out.size();
		// the entry: stored, with the date of 1980-01-01
		Put32(&out, 0x04034B50);
		Put16(&out, 20); Put16(&out, 0); Put16(&out, 0); Put16(&out, 0); Put16(&out, 0x21);
		Put32(&out, crc); Put32(&out, (uint32_t)e.data.size()); Put32(&out, (uint32_t)e.data.size());
		Put16(&out, (unsigned)e.name.size()); Put16(&out, 0);
		out += e.name;
		out += e.data;
		Put32(&directory, 0x02014B50);
		Put16(&directory, 20); Put16(&directory, 20); Put16(&directory, 0); Put16(&directory, 0);
		Put16(&directory, 0); Put16(&directory, 0x21);
		Put32(&directory, crc); Put32(&directory, (uint32_t)e.data.size()); Put32(&directory, (uint32_t)e.data.size());
		Put16(&directory, (unsigned)e.name.size()); Put16(&directory, 0); Put16(&directory, 0);
		Put16(&directory, 0); Put16(&directory, 0); Put32(&directory, 0); Put32(&directory, offset);
		directory += e.name;
	}
	uint32_t directoryOffset = (uint32_t)out.size();
	out += directory;
	Put32(&out, 0x06054B50);
	Put16(&out, 0); Put16(&out, 0);
	Put16(&out, (unsigned)entries.size()); Put16(&out, (unsigned)entries.size());
	Put32(&out, (uint32_t)directory.size()); Put32(&out, directoryOffset);
	Put16(&out, 0);
	return out;
}


std::string
MakeEpub(const Book& book)
{
	std::vector<ZipEntry> entries;
	ZipEntry entry;
	entry.name = "mimetype";
	entry.data = "application/epub+zip";
	entries.push_back(entry);
	entry.name = "META-INF/container.xml";
	entry.data = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<container version=\"1.0\" "
		"xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n<rootfiles>\n<rootfile full-path=\"OEBPS/content.opf\" "
		"media-type=\"application/oebps-package+xml\"/>\n</rootfiles>\n</container>\n";
	entries.push_back(entry);

	std::string identifier = book.isbn.empty() ? "urn:uuid:" + book.identifier : "urn:isbn:" + book.isbn;
	std::string opf = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" "
		"unique-identifier=\"bookid\">\n<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n";
	opf += "<dc:identifier id=\"bookid\">" + Escape(identifier) + "</dc:identifier>\n";
	opf += "<dc:title>" + Escape(book.title) + "</dc:title>\n";
	for (size_t i = 0; i < book.authors.size(); i++)
		opf += "<dc:creator>" + Escape(book.authors[i]) + "</dc:creator>\n";
	opf += "<dc:language>" + Escape(book.language.empty() ? "en" : book.language) + "</dc:language>\n";
	if (!book.publisher.empty())
		opf += "<dc:publisher>" + Escape(book.publisher) + "</dc:publisher>\n";
	if (!book.description.empty())
		opf += "<dc:description>" + Escape(book.description) + "</dc:description>\n";
	for (size_t i = 0; i < book.subjects.size(); i++)
		opf += "<dc:subject>" + Escape(book.subjects[i]) + "</dc:subject>\n";
	if (!book.date.empty())
		opf += "<dc:date>" + Escape(book.date) + "</dc:date>\n";
	if (!book.rights.empty())
		opf += "<dc:rights>" + Escape(book.rights) + "</dc:rights>\n";
	opf += "<meta property=\"dcterms:modified\">2000-01-01T00:00:00Z</meta>\n";
	if (book.coverImage >= 0)
		opf += "<meta name=\"cover\" content=\"image" + Number(book.coverImage + 1, 4) + "\"/>\n";
	opf += "</metadata>\n<manifest>\n";
	opf += "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>\n";
	opf += "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>\n";
	for (size_t i = 0; i < book.chapters.size(); i++)
		opf += "<item id=\"c" + Number(i + 1, 3) + "\" href=\"" + book.chapters[i].name
			+ "\" media-type=\"application/xhtml+xml\"/>\n";
	for (size_t i = 0; i < book.images.size(); i++)
		opf += "<item id=\"image" + Number(i + 1, 4) + "\" href=\"" + book.images[i].name + "\" media-type=\""
			+ book.images[i].type + "\"" + ((int)i == book.coverImage ? " properties=\"cover-image\"" : "") + "/>\n";
	opf += "</manifest>\n<spine toc=\"ncx\">\n";
	for (size_t i = 0; i < book.chapters.size(); i++)
		opf += "<itemref idref=\"c" + Number(i + 1, 3) + "\"/>\n";
	opf += "</spine>\n</package>\n";
	entry.name = "OEBPS/content.opf";
	entry.data = opf;
	entries.push_back(entry);

	// the table of contents, from the headings (or, if there are none, one entry for the book)
	std::vector<TocEntry> toc = book.toc;
	if (toc.empty()) {
		TocEntry all;
		all.title = book.title;
		all.href = book.chapters[0].name;
		all.level = 1;
		toc.push_back(all);
	}
	std::string nav = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<html xmlns=\"http://www.w3.org/1999/xhtml\" "
		"xmlns:epub=\"http://www.idpf.org/2007/ops\">\n<head><title>Contents</title></head>\n<body>\n"
		"<nav epub:type=\"toc\" id=\"toc\"><ol>\n";
	std::string ncx = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">\n"
		"<head><meta name=\"dtb:uid\" content=\"" + Escape(identifier) + "\"/></head>\n<docTitle><text>" + Escape(book.title)
		+ "</text></docTitle>\n<navMap>\n";
	for (size_t i = 0; i < toc.size(); i++) {
		nav += "<li><a href=\"" + toc[i].href + "\">" + Escape(toc[i].title) + "</a></li>\n";
		ncx += "<navPoint id=\"n" + Number(i + 1) + "\" playOrder=\"" + Number(i + 1) + "\"><navLabel><text>"
			+ Escape(toc[i].title) + "</text></navLabel><content src=\"" + toc[i].href + "\"/></navPoint>\n";
	}
	nav += "</ol></nav>\n</body>\n</html>\n";
	ncx += "</navMap>\n</ncx>\n";
	entry.name = "OEBPS/nav.xhtml";
	entry.data = nav;
	entries.push_back(entry);
	entry.name = "OEBPS/toc.ncx";
	entry.data = ncx;
	entries.push_back(entry);

	for (size_t i = 0; i < book.chapters.size(); i++) {
		entry.name = "OEBPS/" + book.chapters[i].name;
		entry.data = book.chapters[i].xhtml;
		entries.push_back(entry);
	}
	for (size_t i = 0; i < book.images.size(); i++) {
		entry.name = "OEBPS/" + book.images[i].name;
		entry.data = book.images[i].data;
		entries.push_back(entry);
	}
	return MakeZip(entries);
}

}	// namespace Mobi
