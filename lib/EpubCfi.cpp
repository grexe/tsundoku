/*
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Gregor B. Rosenauer & Claude
 */
#include "EpubCfi.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "EpubInfo.h"


// ---- the text of a content document, as the words come from a page

namespace {

struct FlatText {
	std::string           text;
	std::vector<xmlNode*> nodeOf;	// for each byte: the text node it comes from (NULL for a space between blocks)
	std::vector<int>      rawOf;	// and where in the content of that node the character starts
};


bool
IsTextLike(const xmlNode* node)
{
	return node != NULL && (node->type == XML_TEXT_NODE || node->type == XML_CDATA_SECTION_NODE);
}


bool
NameIs(const xmlNode* node, const char* name)
{
	return node->type == XML_ELEMENT_NODE && strcmp((const char*)node->name, name) == 0;
}


bool
IsBlock(const xmlNode* node)
{
	static const char* const kBlocks[] = { "p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "li", "ul", "ol", "dl",
		"dt", "dd", "blockquote", "pre", "section", "article", "aside", "header", "footer", "nav", "table", "tr",
		"td", "th", "figure", "figcaption", "body", "br", "hr", "address", "caption", "aside", NULL };
	for (int i = 0; kBlocks[i] != NULL; i++) {
		if (NameIs(node, kBlocks[i]))
			return true;
	}
	return false;
}


bool
IsHidden(const xmlNode* node)
{
	return NameIs(node, "head") || NameIs(node, "script") || NameIs(node, "style") || NameIs(node, "title");
}


void
Boundary(FlatText& flat)
{
	if (!flat.text.empty() && flat.text[flat.text.size() - 1] != ' ') {
		flat.text.push_back(' ');
		flat.nodeOf.push_back(NULL);
		flat.rawOf.push_back(0);
	}
}


// the length of the UTF-8 character that begins with this byte
int
CharLength(unsigned char lead)
{
	if (lead < 0x80)
		return 1;
	if ((lead & 0xe0) == 0xc0)
		return 2;
	if ((lead & 0xf0) == 0xe0)
		return 3;
	if ((lead & 0xf8) == 0xf0)
		return 4;
	return 1;
}


// the UTF-16 units of the characters in the first byteCount bytes of the text
int
Utf16Units(const char* text, int byteCount)
{
	int units = 0;
	for (int i = 0; i < byteCount;) {
		int length = CharLength((unsigned char)text[i]);
		units += length == 4 ? 2 : 1;
		i += length;
	}
	return units;
}


// ... and the other way round: how many bytes hold that many units
int
BytesForUnits(const char* text, int length, int units)
{
	int at = 0, counted = 0;
	while (at < length && counted < units) {
		int step = CharLength((unsigned char)text[at]);
		counted += step == 4 ? 2 : 1;
		at += step;
	}
	return at < length ? at : length;
}


// A space is any white space, also the non-breaking one; a soft hyphen is nothing.
// Returns the number of bytes of the character that is at the text, sets space or skip.
int
Classify(const char* text, int remaining, bool* space, bool* skip)
{
	*space = false;
	*skip = false;
	unsigned char c = (unsigned char)text[0];
	if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f') {
		*space = true;
		return 1;
	}
	if (c == 0xc2 && remaining >= 2) {
		unsigned char d = (unsigned char)text[1];
		if (d == 0xa0) {
			*space = true;
			return 2;
		}
		if (d == 0xad) {
			*skip = true;
			return 2;
		}
	}
	if (c == 0xe2 && remaining >= 3 && (unsigned char)text[1] == 0x80) {
		unsigned char d = (unsigned char)text[2];
		if ((d >= 0x80 && d <= 0x8a) || d == 0xaf) {	// the spaces of various widths
			*space = true;
			return 3;
		}
		if (d == 0x8b) {	// zero width space
			*skip = true;
			return 3;
		}
	}
	return CharLength(c);
}


void
Collect(xmlNode* node, FlatText& flat)
{
	for (; node != NULL; node = node->next) {
		if (IsTextLike(node)) {
			const char* content = (const char*)node->content;
			if (content == NULL)
				continue;
			int length = (int)strlen(content);
			for (int at = 0; at < length;) {
				bool space, skip;
				int step = Classify(content + at, length - at, &space, &skip);
				if (skip) {
					at += step;
					continue;
				}
				if (space) {
					if (!flat.text.empty() && flat.text[flat.text.size() - 1] != ' ') {
						flat.text.push_back(' ');
						flat.nodeOf.push_back(node);
						flat.rawOf.push_back(at);
					}
				} else {
					for (int k = 0; k < step && at + k < length; k++) {
						flat.text.push_back(content[at + k]);
						flat.nodeOf.push_back(node);
						flat.rawOf.push_back(at);
					}
				}
				at += step;
			}
		} else if (node->type == XML_ELEMENT_NODE && !IsHidden(node)) {
			bool block = IsBlock(node);
			if (block)
				Boundary(flat);
			Collect(node->children, flat);
			if (block)
				Boundary(flat);
		}
	}
}


std::string
Normalize(const char* text)
{
	std::string result;
	int length = (int)strlen(text);
	for (int at = 0; at < length;) {
		bool space, skip;
		int step = Classify(text + at, length - at, &space, &skip);
		if (skip) {
			at += step;
			continue;
		}
		if (space) {
			if (!result.empty() && result[result.size() - 1] != ' ')
				result.push_back(' ');
		} else
			result.append(text + at, step > length - at ? length - at : step);
		at += step;
	}
	while (!result.empty() && result[result.size() - 1] == ' ')
		result.erase(result.size() - 1);
	return result;
}


// the content document of a spine item, NULL if it cannot be read
xmlDoc*
LoadContent(const char* container, const EpubInfo& info, int spineIndex)
{
	if (spineIndex < 0 || spineIndex >= (int)info.spine.size())
		return NULL;
	std::vector<uint8> data;
	if (!EpubInfo::ReadMember(container, info.spine[spineIndex].path.String(), &data) || data.empty())
		return NULL;
	return xmlReadMemory((const char*)&data[0], (int)data.size(), NULL, NULL,
		XML_PARSE_RECOVER | XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
}


// ---- steps

std::string
Number(int number)
{
	char buffer[16];
	snprintf(buffer, sizeof(buffer), "%d", number);
	return buffer;
}


std::string
EscapeAssertion(const std::string& value)
{
	std::string result;
	for (size_t i = 0; i < value.size(); i++) {
		char c = value[i];
		if (strchr("^[](),;=", c) != NULL)
			result.push_back('^');
		result.push_back(c);
	}
	return result;
}


std::string
ElementStep(xmlNode* element)
{
	int index = 0;
	for (xmlNode* node = element->parent->children; node != NULL; node = node->next) {
		if (node->type == XML_ELEMENT_NODE) {
			index++;
			if (node == element)
				break;
		}
	}
	std::string step = "/" + Number(2 * index);
	xmlChar* id = xmlGetProp(element, (const xmlChar*)"id");
	if (id != NULL) {
		if (id[0] != '\0')
			step += "[" + EscapeAssertion((const char*)id) + "]";
		xmlFree(id);
	}
	return step;
}


// the steps down to a place in a text node, and the offset in the text (in UTF-16 units, from the beginning of the
// chunk of adjacent text nodes)
void
StepsTo(xmlNode* text, int rawOffset, xmlNode* root, std::vector<std::string>* steps, int* offset)
{
	xmlNode* first = text;
	while (IsTextLike(first->prev))
		first = first->prev;

	int elementsBefore = 0;
	for (xmlNode* node = text->parent->children; node != NULL && node != first; node = node->next) {
		if (node->type == XML_ELEMENT_NODE)
			elementsBefore++;
	}

	int units = 0;
	for (xmlNode* node = first; node != text; node = node->next) {
		if (node->content != NULL)
			units += Utf16Units((const char*)node->content, (int)strlen((const char*)node->content));
	}
	if (text->content != NULL)
		units += Utf16Units((const char*)text->content, rawOffset);
	*offset = units;

	std::vector<std::string> reversed;
	reversed.push_back("/" + Number(2 * elementsBefore + 1));
	for (xmlNode* element = text->parent; element != NULL && element != root; element = element->parent) {
		if (element->type != XML_ELEMENT_NODE)
			break;
		reversed.push_back(ElementStep(element));
	}
	steps->assign(reversed.rbegin(), reversed.rend());
}


struct Step {
	int         number;
	std::string assertion;
};

struct ParsedPath {
	std::vector<Step> package;
	std::vector<Step> content;
	int               offset;
	ParsedPath() : offset(-1) {}
};


// the assertion in brackets that follows, ^ escapes the next character
void
ReadAssertion(const std::string& path, size_t* at, std::string* assertion)
{
	if (*at < path.size() && path[*at] == '[') {
		(*at)++;
		while (*at < path.size() && path[*at] != ']') {
			if (path[*at] == '^' && *at + 1 < path.size())
				(*at)++;
			assertion->push_back(path[*at]);
			(*at)++;
		}
		if (*at < path.size())
			(*at)++;
	}
}


bool
ParsePath(const std::string& path, ParsedPath* parsed)
{
	bool afterBang = false;
	size_t at = 0;
	while (at < path.size()) {
		char c = path[at];
		if (c == '!') {
			afterBang = true;
			at++;
		} else if (c == '/') {
			at++;
			Step step;
			step.number = 0;
			bool digits = false;
			while (at < path.size() && path[at] >= '0' && path[at] <= '9') {
				step.number = step.number * 10 + (path[at] - '0');
				digits = true;
				at++;
			}
			if (!digits)
				return false;
			ReadAssertion(path, &at, &step.assertion);
			(afterBang ? parsed->content : parsed->package).push_back(step);
		} else if (c == ':') {
			at++;
			int offset = 0;
			bool digits = false;
			while (at < path.size() && path[at] >= '0' && path[at] <= '9') {
				offset = offset * 10 + (path[at] - '0');
				digits = true;
				at++;
			}
			if (!digits)
				return false;
			parsed->offset = offset;
			std::string ignored;
			ReadAssertion(path, &at, &ignored);
		} else
			return false;
	}
	return !parsed->package.empty();
}


xmlNode*
FindById(xmlNode* from, const std::string& id)
{
	for (xmlNode* node = from; node != NULL; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;
		xmlChar* value = xmlGetProp(node, (const xmlChar*)"id");
		bool found = value != NULL && id == (const char*)value;
		if (value != NULL)
			xmlFree(value);
		if (found)
			return node;
		if (xmlNode* inside = FindById(node->children, id))
			return inside;
	}
	return NULL;
}


// the spine item that a path of the package document leads to
int
SpineIndexFor(const EpubInfo& info, const std::vector<Step>& package)
{
	if (package.size() < 2)
		return -1;
	const Step& itemref = package[1];
	int found = -1;
	for (size_t i = 0; i < info.spine.size(); i++) {
		if (info.spine[i].step == itemref.number)
			found = (int)i;
	}
	// the id of the itemref is the surer way to it
	if (!itemref.assertion.empty()
		&& (found < 0 || info.spine[found].idref != itemref.assertion.c_str())) {
		for (size_t i = 0; i < info.spine.size(); i++) {
			if (info.spine[i].idref == itemref.assertion.c_str())
				return (int)i;
		}
	}
	return found;
}


// the text node and the offset in its content that a path in the content document leads to
bool
PlaceFor(xmlDoc* doc, const std::vector<Step>& steps, int offset, xmlNode** _text, int* _rawOffset)
{
	xmlNode* current = xmlDocGetRootElement(doc);
	if (current == NULL)
		return false;

	for (size_t i = 0; i < steps.size(); i++) {
		const Step& step = steps[i];
		if (step.number % 2 == 0) {
			int wanted = step.number / 2;
			int index = 0;
			xmlNode* element = NULL;
			for (xmlNode* node = current->children; node != NULL; node = node->next) {
				if (node->type == XML_ELEMENT_NODE && ++index == wanted) {
					element = node;
					break;
				}
			}
			if (!step.assertion.empty()) {
				xmlChar* id = element != NULL ? xmlGetProp(element, (const xmlChar*)"id") : NULL;
				bool same = id != NULL && step.assertion == (const char*)id;
				if (id != NULL)
					xmlFree(id);
				if (!same) {
					// not the element that the CFI was made for: the one with that id
					xmlNode* byId = FindById(current->children, step.assertion);
					if (byId != NULL)
						element = byId;
				}
			}
			if (element == NULL)
				return false;
			current = element;
		} else {
			// a chunk of text: after this many elements
			int elementsBefore = (step.number - 1) / 2;
			int seen = 0;
			xmlNode* first = NULL;
			for (xmlNode* node = current->children; node != NULL; node = node->next) {
				if (node->type == XML_ELEMENT_NODE) {
					seen++;
					continue;
				}
				if (IsTextLike(node) && seen == elementsBefore) {
					first = node;
					break;
				}
			}
			if (first == NULL)
				return false;
			int remaining = offset < 0 ? 0 : offset;
			for (xmlNode* node = first; IsTextLike(node); node = node->next) {
				const char* content = node->content != NULL ? (const char*)node->content : "";
				int length = (int)strlen(content);
				int units = Utf16Units(content, length);
				if (remaining <= units || !IsTextLike(node->next)) {
					*_text = node;
					*_rawOffset = BytesForUnits(content, length, remaining);
					return true;
				}
				remaining -= units;
			}
			return false;
		}
	}

	// the path ended at an element: its first text
	for (xmlNode* node = current; node != NULL;) {
		if (IsTextLike(node)) {
			*_text = node;
			*_rawOffset = 0;
			return true;
		}
		if (node->children != NULL)
			node = node->children;
		else {
			while (node != NULL && node != current && node->next == NULL)
				node = node->parent;
			if (node == NULL || node == current)
				return false;
			node = node->next;
		}
	}
	return false;
}


// the index in the text of a place in a text node; the place after the end of the node gives the end of its text
int
IndexFor(const FlatText& flat, xmlNode* node, int rawOffset)
{
	int last = -1;
	for (size_t b = 0; b < flat.nodeOf.size(); b++) {
		if (flat.nodeOf[b] != node)
			continue;
		if (flat.rawOf[b] >= rawOffset)
			return (int)b;
		last = (int)b;
	}
	return last >= 0 ? last + 1 : -1;
}

}	// namespace


bool
EpubCfi::Create(const char* container, const EpubInfo& info, int spineIndex, const char* words, float fraction,
	BString* cfi, BString* prefix, BString* suffix)
{
	xmlDoc* doc = LoadContent(container, info, spineIndex);
	if (doc == NULL)
		return false;

	bool ok = false;
	xmlNode* root = xmlDocGetRootElement(doc);
	FlatText flat;
	std::string wanted = Normalize(words);
	if (root != NULL && !wanted.empty()) {
		Collect(root->children, flat);

		// the place where the words are; if there are several, the one nearest to where they were expected
		long best = -1;
		double target = fraction * (double)flat.text.size();
		for (size_t at = flat.text.find(wanted); at != std::string::npos; at = flat.text.find(wanted, at + 1)) {
			if (best < 0 || fabs((double)at - target) < fabs((double)best - target))
				best = (long)at;
		}
		if (best >= 0) {
			size_t start = (size_t)best;
			size_t end = start + wanted.size();	// after the last byte
			size_t lastStart = end - 1;
			while (lastStart > start && ((unsigned char)flat.text[lastStart] & 0xc0) == 0x80)
				lastStart--;
			xmlNode* startNode = flat.nodeOf[start];
			xmlNode* endNode = flat.nodeOf[lastStart];
			if (startNode != NULL && endNode != NULL) {
				int endRaw = flat.rawOf[lastStart] + CharLength((unsigned char)flat.text[lastStart]);
				std::vector<std::string> startSteps, endSteps;
				int startOffset = 0, endOffset = 0;
				StepsTo(startNode, flat.rawOf[start], root, &startSteps, &startOffset);
				StepsTo(endNode, endRaw, root, &endSteps, &endOffset);

				size_t common = 0;
				while (common < startSteps.size() && common < endSteps.size()
					&& startSteps[common] == endSteps[common])
					common++;

				const EpubInfo::SpineItem& item = info.spine[spineIndex];
				std::string parent = "/" + Number(info.spineStep) + "/" + Number(item.step);
				if (!item.idref.IsEmpty())
					parent += "[" + EscapeAssertion(item.idref.String()) + "]";
				parent += "!";
				std::string head, from, to;
				for (size_t i = 0; i < common; i++)
					head += startSteps[i];
				for (size_t i = common; i < startSteps.size(); i++)
					from += startSteps[i];
				for (size_t i = common; i < endSteps.size(); i++)
					to += endSteps[i];
				from += ":" + Number(startOffset);
				to += ":" + Number(endOffset);

				std::string result = "epubcfi(" + parent + head + "," + from + "," + to + ")";
				cfi->SetTo(result.c_str());
				if (prefix != NULL) {
					// about 32 characters before and after, from the beginning of a character
					size_t from32 = start > 32 ? start - 32 : 0;
					while (from32 < start && ((unsigned char)flat.text[from32] & 0xc0) == 0x80)
						from32++;
					std::string before = flat.text.substr(from32, start - from32);
					prefix->SetTo(before.c_str());
				}
				if (suffix != NULL) {
					size_t to32 = end + 32 < flat.text.size() ? end + 32 : flat.text.size();
					while (to32 < flat.text.size() && ((unsigned char)flat.text[to32] & 0xc0) == 0x80)
						to32++;
					std::string after = flat.text.substr(end, to32 - end);
					suffix->SetTo(after.c_str());
				}
				ok = true;
			}
		}
	}
	xmlFreeDoc(doc);
	return ok;
}


bool
EpubCfi::Resolve(const char* container, const EpubInfo& info, const char* cfi, int* spineIndex, BString* words,
	float* fraction)
{
	std::string text(cfi);
	if (text.compare(0, 8, "epubcfi(") != 0 || text.empty() || text[text.size() - 1] != ')')
		return false;
	text = text.substr(8, text.size() - 9);

	// the parts: parent, start and end, split at the commas that are not in an assertion
	std::vector<std::string> parts(1);
	int depth = 0;
	for (size_t i = 0; i < text.size(); i++) {
		char c = text[i];
		if (c == '^' && i + 1 < text.size()) {
			parts.back().push_back(c);
			parts.back().push_back(text[++i]);
			continue;
		}
		if (c == '[')
			depth++;
		else if (c == ']')
			depth--;
		if (c == ',' && depth == 0)
			parts.push_back(std::string());
		else
			parts.back().push_back(c);
	}
	bool isRange = parts.size() == 3;
	if (parts.size() != 1 && !isRange)
		return false;

	ParsedPath start, end;
	if (!ParsePath(parts[0] + (isRange ? parts[1] : std::string()), &start))
		return false;
	if (isRange && !ParsePath(parts[0] + parts[2], &end))
		return false;

	int index = SpineIndexFor(info, start.package);
	if (index < 0)
		return false;
	xmlDoc* doc = LoadContent(container, info, index);
	if (doc == NULL)
		return false;

	bool ok = false;
	xmlNode* root = xmlDocGetRootElement(doc);
	if (root != NULL) {
		FlatText flat;
		Collect(root->children, flat);

		xmlNode* startNode = NULL;
		xmlNode* endNode = NULL;
		int startRaw = 0, endRaw = 0;
		if (PlaceFor(doc, start.content, start.offset, &startNode, &startRaw)) {
			int from = IndexFor(flat, startNode, startRaw);
			int to = -1;
			if (isRange && PlaceFor(doc, end.content, end.offset, &endNode, &endRaw))
				to = IndexFor(flat, endNode, endRaw);
			if (from >= 0) {
				if (to < from) {
					// a point: the words that follow it, to the end of the word after about 80 bytes
					to = from + 80;
					if (to > (int)flat.text.size())
						to = (int)flat.text.size();
					while (to < (int)flat.text.size() && flat.text[to] != ' ')
						to++;
				}
				std::string covered = flat.text.substr(from, to - from);
				while (!covered.empty() && covered[0] == ' ')
					covered.erase(0, 1);
				while (!covered.empty() && covered[covered.size() - 1] == ' ')
					covered.erase(covered.size() - 1);
				*spineIndex = index;
				words->SetTo(covered.c_str());
				*fraction = flat.text.empty() ? 0 : (float)from / (float)flat.text.size();
				ok = true;
			}
		}
	}
	xmlFreeDoc(doc);
	return ok;
}
