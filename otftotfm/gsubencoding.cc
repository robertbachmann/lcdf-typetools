/* gsubencoding.{cc,hh} -- an encoding during and after OpenType features
 *
 * Copyright (c) 2003 Eddie Kohler
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version. This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "gsubencoding.hh"
#include "dvipsencoding.hh"
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <lcdf/straccum.hh>

GsubEncoding::GsubEncoding(int nglyphs)
    : _boundary_glyph(nglyphs)
{
    _encoding.assign(256, 0);
}

bool
GsubEncoding::setting(int code, Vector<Setting> &v) const
{
    v.clear();
    if (code < 0 || code >= _encoding.size())
	return false;

    // find _vfpos entry, if any
    int pdx = 0, pdy = 0, adx = 0;
    for (int vfp = 0; vfp < _vfpos.size(); vfp++)
	if (_vfpos[vfp].in == code) {
	    pdx = _vfpos[vfp].pdx, pdy = _vfpos[vfp].pdy, adx = _vfpos[vfp].adx;
	    break;
	}

    // XXX find _fake_ligatures entry

    if (_encoding[code] <= 0)
	return false;
    
    if (pdx != 0)
	v.push_back(Setting(Setting::HMOVETO, pdx));
    if (pdy != 0)
	v.push_back(Setting(Setting::VMOVETO, pdy));
    v.push_back(Setting(Setting::SHOW, _encoding[code]));
    if (pdy != 0)
	v.push_back(Setting(Setting::VMOVETO, -pdy));
    if (adx - pdx != 0)
	v.push_back(Setting(Setting::HMOVETO, adx - pdx));
    return true;
}

int
GsubEncoding::hard_encoding(Glyph g) const
{
    if (g < 0)
	return -1;
    int answer = -1, n = 0;
    for (int i = _encoding.size() - 1; i >= 0; i--)
	if (_encoding[i] == g)
	    answer = i, n++;
    if (n < 2) {
	if (g >= _emap.size())
	    _emap.resize(g + 1, -2);
	_emap[g] = answer;
    }
    return answer;
}

int
GsubEncoding::force_encoding(Glyph g)
{
    int e = encoding(g);
    if (e >= 0)
	return e;
    else {
	_encoding.push_back(g);
	assign_emap(g, _encoding.size() - 1);
	return _encoding.size() - 1;
    }
}

void
GsubEncoding::encode(int code, Glyph g)
{
    assert(code >= 0 && g >= 0);
    if (code >= _encoding.size())
	_encoding.resize(code + 1, 0);
    _encoding[code] = g;
    assign_emap(g, code);
}


enum { CH_NO = 0, CH_SOME = 1, CH_ALL = 2 };

static void
assign_changed_context(Vector<int> &changed, Vector<int *> &changed_context, int e1, int e2)
{
    int n = changed_context.size();
    if (e1 >= 0 && e2 >= 0 && e1 < n && e2 < n) {
	int *v = changed_context[e1];
	if (!v) {
	    v = changed_context[e1] = new int[((n - 1) >> 5) + 1];
	    memset(v, 0, sizeof(int) * (((n - 1) >> 5) + 1));
	}
	v[e2 >> 5] |= (1 << (e2 & 0x1F));
	assert(changed[e1] != CH_ALL);
	changed[e1] = CH_SOME;
    }
}

static bool
in_changed_context(const Vector<int> &changed, const Vector<int *> &changed_context, int e1, int e2)
{
    int n = changed_context.size();
    if (e1 >= 0 && e2 >= 0 && e1 < n && e2 < n) {
	if (changed[e1] == CH_ALL)
	    return true;
	else if (const int *v = changed_context[e1])
	    return (v[e2 >> 5] & (1 << (e2 & 0x1F))) != 0;
    }
    return false;
}

void
GsubEncoding::add_single_context_substitution(int left, int right, int out, bool is_right)
{
    if (out != (is_right ? right : left)) {
	Ligature l;
	l.in.push_back(left);
	l.in.push_back(right);
	l.out = out;
	l.skip = 1;
	l.context = (is_right ? 1 : -1);
	_ligatures.push_back(l);
    }
}

int
GsubEncoding::apply(const Vector<Substitution> &sv, bool allow_single)
{
    // keep track of what substitutions we have performed
    Vector<int> changed(_encoding.size(), CH_NO);
    Vector<int *> changed_context(_encoding.size(), 0);

    // XXX encodings that encode the same glyph multiple times?
    
    // loop over substitutions
    int success = 0;
    for (int i = 0; i < sv.size(); i++) {
	const Substitution &s = sv[i];
	if ((s.is_single() || s.is_alternate()) && allow_single) {
	    int e = encoding(s.in_glyph());
	    if (e < 0 || e >= changed.size())
		/* not encoded before this substitution began, ignore */;
	    else if (changed[e] == CH_NO) {
		// no one has changed this glyph yet, change it unilaterally
		assign_emap(s.in_glyph(), -2);
		assign_emap(s.out_glyph_0(), e);
		_encoding[e] = s.out_glyph_0();
		changed[e] = CH_ALL;
	    } else if (changed[e] == CH_SOME) {
		// some contextual substitutions have changed this glyph, add
		// contextual substitutions for the remaining possibilities
		int out = force_encoding(s.out_glyph_0());
		const int *v = changed_context[e];
		for (int j = 0; j < changed.size(); j++)
		    if (_encoding[j] > 0 && !(v[j >> 5] & (1 << (j & 0x1F))))
			add_single_context_substitution(e, j, out, true);
		changed[e] = CH_ALL;
	    }
	    success++;
	
	} else if (s.is_ligature()) {
	    Vector<Glyph> in;
	    s.in_glyphs(in);
	    Ligature l;
	    // XXX multi encoded glyphs?
	    for (int i = 0; i < in.size(); i++) {
		int e = encoding(in[i]);
		if (e < 0 || e >= changed.size() || changed[e] == CH_ALL)
		    goto ligature_fail;
		l.in.push_back(e);
	    }
	    l.out = force_encoding(s.out_glyph());
	    l.skip = 1;
	    l.context = 0;
	    _ligatures.push_back(l);
	  ligature_fail:
	    success++;

	} else if (s.is_single_rcontext()) {
	    int in = encoding(s.in_glyph()), right = encoding(s.right_glyph());
	    if (in >= 0 && in < changed.size()
		&& right >= 0 && right < changed.size()
		&& !in_changed_context(changed, changed_context, in, right)) {
		add_single_context_substitution(in, right, force_encoding(s.out_glyph()), true);
		assign_changed_context(changed, changed_context, in, right);
	    }
	    success++;

	} else if (s.is_single_lcontext()) {
	    int left = encoding(s.left_glyph()), in = encoding(s.in_glyph());
	    if (in >= 0 && in < changed.size()
		&& left >= 0 && left < changed.size()
		&& !in_changed_context(changed, changed_context, left, in)) {
		add_single_context_substitution(left, in, force_encoding(s.out_glyph()), false);
		assign_changed_context(changed, changed_context, left, in);
	    }
	    success++;
	}
    }

    for (int i = 0; i < changed_context.size(); i++)
	delete[] changed_context[i];
    return success;
}

static bool			// returns old value
assign_bitvec(int *&bitvec, int e, int n)
{
    if (e >= 0 && e < n) {
	if (!bitvec) {
	    bitvec = new int[((n - 1) >> 5) + 1];
	    memset(bitvec, 0, sizeof(int) * (((n - 1) >> 5) + 1));
	}
	bool result = (bitvec[e >> 5] & (1 << (e & 0x1F))) != 0;
	bitvec[e >> 5] |= (1 << (e & 0x1F));
	return result;
    } else
	return false;
}

static bool
in_bitvec(const int *bitvec, int e, int n)
{
    if (e >= 0 && e < n && bitvec)
	return (bitvec[e >> 5] & (1 << (e & 0x1F))) != 0;
    else
	return false;
}

int
GsubEncoding::apply(const Vector<Positioning> &pv)
{
    // keep track of what substitutions we have performed
    int *single_changed = 0;
    Vector<int *> pair_changed(_encoding.size(), 0);

    // XXX encodings that encode the same glyph multiple times?
    
    // loop over substitutions
    int success = 0;
    for (int i = 0; i < pv.size(); i++) {
	const Positioning &p = pv[i];
	if (p.is_pairkern()) {
	    int code1 = encoding(p.left_glyph());
	    int code2 = encoding(p.right_glyph());
	    if (code1 >= 0 && code2 >= 0
		&& !assign_bitvec(pair_changed[code1], code2, _encoding.size()))
		add_kern(code1, code2, p.left().adx);
	    success++;
	} else if (p.is_single()) {
	    int code = encoding(p.left_glyph());
	    if (code >= 0 && !assign_bitvec(single_changed, code, _encoding.size()))
		add_single_positioning(code, p.left().pdx, p.left().pdy, p.left().adx);
	    success++;
	}
    }

    delete[] single_changed;
    for (int i = 0; i < pair_changed.size(); i++)
	delete[] pair_changed[i];
    return success;
}

int
GsubEncoding::find_skippable_twoligature(int a, int b, bool add_fake)
{
    for (int i = 0; i < _ligatures.size(); i++) {
	const Ligature &l = _ligatures[i];
	if (l.in.size() == 2 && l.in[0] == a && l.in[1] == b
	    && l.skip == 0 && l.context == 0)
	    return l.out;
    }
    if (add_fake) {
	_encoding.push_back(FAKE_LIGATURE);
	Ligature fakel;
	fakel.in.push_back(a);
	fakel.in.push_back(b);
	fakel.out = _encoding.size() - 1;
	fakel.skip = 0;
	fakel.context = 0;
	_fake_ligatures.push_back(fakel);
	return fakel.out;
    } else
	return -1;
}

void
GsubEncoding::simplify_ligatures(bool add_fake)
{
    // mark ligatures as skippable
    for (int i = 0; i < _ligatures.size(); i++) {
	int c = _ligatures[i].out;
	for (int j = 0; j < _ligatures.size(); j++)
	    if (_ligatures[j].in[0] == c)
		goto must_skip;
	_ligatures[i].skip = 0;
      must_skip: ;
    }

    // actually simplify
    for (int i = 0; i < _ligatures.size(); i++) {
	Ligature &l = _ligatures[i];
	while (l.in.size() > 2) {
	    int l2 = find_skippable_twoligature(l.in[0], l.in[1], add_fake);
	    // might be < 0
	    l.in[0] = l2;
	    memmove(&l.in[1], &l.in[2], (l.in.size() - 2) * sizeof(Glyph));
	    l.in.pop_back();
	}
    }

    // remove redundant ligatures
    for (int i = 0; i < _ligatures.size(); i++) {
	const Ligature &l = _ligatures[i];
	if (l.in[0] < 0)
	    continue;
	for (int j = i + 1; j < _ligatures.size(); j++) {
	    Ligature &ll = _ligatures[j];
	    if (ll.in.size() >= l.in.size()
		&& memcmp(&ll.in[0], &l.in[0], l.in.size() * sizeof(Glyph)) == 0)
		ll.in[0] = -1;
	}
    }

    // remove null ligatures, which can creep in to override following
    // ligatures in the table
    for (int i = 0; i < _ligatures.size(); i++) {
	Ligature &l = _ligatures[i];
	if (l.in[0] >= 0 && l.context >= 0 && l.in.size() == l.context + 1
	    && l.in[0] == l.out)
	    l.in[0] = -1;
    }
}

void
GsubEncoding::simplify_positionings()
{
    if (_kerns.size()) {
	// combine kerns for the same set of characters
	std::sort(_kerns.begin(), _kerns.end());
	int delta;
	for (int i = 0; i + 1 < _kerns.size(); i += delta) {
	    Kern &k1 = _kerns[i];
	    for (delta = 1; i + delta < _kerns.size(); delta++) {
		Kern &k2 = _kerns[i + delta];
		if (k1.left != k2.left || k1.right != k2.right)
		    break;
		k1.amount += k2.amount;
		k2.left = -1;
	    }
	}
    }

    if (_vfpos.size()) {
	// combine positionings for the same character
	std::sort(_vfpos.begin(), _vfpos.end());
	int delta;
	for (int i = 0; i + 1 < _vfpos.size(); i += delta) {
	    Vfpos &p1 = _vfpos[i];
	    for (delta = 1; i + delta < _vfpos.size(); delta++) {
		Vfpos &p2 = _vfpos[i + delta];
		if (p1.in != p2.in)
		    break;
		p1.pdx += p2.pdx;
		p1.pdy += p2.pdy;
		p1.adx += p2.adx;
		p2.in = -1;
	    }
	}
    }
}

namespace {
struct Slot { int position, new_position, value; };

static bool
operator<(const Slot &a, const Slot &b)
{
    return a.value < b.value;
}
}

void
GsubEncoding::reassign_ligature(Ligature &l, const Vector<int> &reassignment)
{
    for (int j = 0; j < l.in.size(); j++)
	l.in[j] = reassignment[l.in[j] + 1];
    l.out = reassignment[l.out + 1];
}

void
GsubEncoding::reassign_codes(const Vector<int> &reassignment)
{
    // reassign code points in ligature_data vector
    for (int i = 0; i < _ligatures.size(); i++)
	reassign_ligature(_ligatures[i], reassignment);
    for (int i = 0; i < _fake_ligatures.size(); i++)
	reassign_ligature(_fake_ligatures[i], reassignment);

    // reassign code points in kern vector
    for (int i = 0; i < _kerns.size(); i++) {
	_kerns[i].left = reassignment[_kerns[i].left + 1];
	_kerns[i].right = reassignment[_kerns[i].right + 1];
    }
    
    // reassign code points in virtual positioning vector
    for (int i = 0; i < _vfpos.size(); i++)
	_vfpos[i].in = reassignment[_vfpos[i].in + 1];

    // mark that _emap is worthless
    _emap.clear();
}

void
GsubEncoding::cut_encoding(int size)
{
    if (_encoding.size() <= size)
	_encoding.resize(size, 0);
    else {
	// reassign codes
	Vector<int> reassignment(_encoding.size() + 1, -1);
	for (int i = -1; i < size; i++)
	    reassignment[i+1] = i;
	for (int i = size; i < _encoding.size(); i++)
	    reassignment[i+1] = -1;
	reassign_codes(reassignment);

	// shrink encoding for real
	_encoding.resize(size, 0);
    }
}

void
GsubEncoding::shrink_encoding(int size, const DvipsEncoding &dvipsenc, const Vector<PermString> &glyph_names, ErrorHandler *errh)
{
    if (_encoding.size() <= size) {
	_encoding.resize(size, 0);
	return;
    }
    
    // collect larger values
    Vector<Slot> slots;
    for (int i = size; i < _encoding.size(); i++)
	if (_encoding[i]) {
	    Slot p = { i, -1, _encoding[i] };
	    slots.push_back(p);
	}
    // sort them by value
    std::sort(slots.begin(), slots.end());

    // insert ligatures into encoding holes

    // first, prefer their old slots, if available
    for (int slotnum = 0; slotnum < slots.size(); slotnum++)
	if (PermString g = glyph_names[slots[slotnum].value]) {
	    int e = dvipsenc.encoding_of(g);
	    if (e >= 0 && !_encoding[e]) {
		_encoding[e] = slots[slotnum].value;
		slots[slotnum].new_position = e;
	    }
	}

    // next, loop over all empty slots
    {
	int slotnum = 0, e = 0;
	bool avoid = true;
	while (slotnum < slots.size() && e < size)
	    if (slots[slotnum].new_position >= 0)
		slotnum++;
	    else if (!_encoding[e] && (!avoid || !dvipsenc.encoded(e))) {
		_encoding[e] = slots[slotnum].value;
		slots[slotnum].new_position = e;
		e++;
		slotnum++;
	    } else {
		e++;
		if (e >= size && avoid)
		    avoid = false, e = 0;
	    }

	// complain if they can't fit
	if (slotnum < slots.size()) {
	    // collect names of unencoded glyphs
	    Vector<String> unencoded;
	    for (int s = slotnum; s < slots.size(); s++)
		unencoded.push_back(glyph_names[ _encoding[ slots[s].position ] ]);
	    std::sort(unencoded.begin(), unencoded.end());
	    StringAccum sa;
	    sa.append_fill_lines(unencoded, 68, "", "  ");
	    errh->lwarning(" ", (unencoded.size() == 1 ? "ignoring unencodable glyphs:" : "ignoring unencodable glyphs:"));
	    errh->lmessage(" ", "%s(\
This encoding doesn't have enough room for all the glyphs used by\n\
the font, so I've ignored those listed above.)", sa.c_str());
	}
    }

    // reassign codes
    Vector<int> reassignment(_encoding.size() + 1, -1);
    for (int i = -1; i < size; i++)
	reassignment[i+1] = i;
    for (int slotnum = 0; slotnum < slots.size(); slotnum++)
	reassignment[slots[slotnum].position+1] = slots[slotnum].new_position;
    reassign_codes(reassignment);

    // finally, shrink encoding for real
    _encoding.resize(size, 0);
}

void
GsubEncoding::add_twoligature(int code1, int code2, int outcode)
{
    Ligature l;
    l.in.push_back(code1);
    l.in.push_back(code2);
    l.out = outcode;
    l.skip = 0;
    l.context = 0;
    _ligatures.push_back(l);
}

void
GsubEncoding::add_kern(int left, int right, int amount)
{
    Kern k;
    k.left = left;
    k.right = right;
    k.amount = amount;
    _kerns.push_back(k);
}

void
GsubEncoding::add_single_positioning(int code, int pdx, int pdy, int adx)
{
    Vfpos p;
    p.in = code;
    p.pdx = pdx;
    p.pdy = pdy;
    p.adx = adx;
    _vfpos.push_back(p);
}

void
GsubEncoding::remove_ligatures(int code1, int code2)
{
    for (int i = 0; i < _ligatures.size(); i++) {
	Ligature &l = _ligatures[i];
	if (l.in.size() == 2
	    && (code1 == CODE_ALL || l.in[0] == code1)
	    && (code2 == CODE_ALL || l.in[1] == code2))
	    l.in[0] = -1;
    }
}

void
GsubEncoding::remove_kerns(int code1, int code2)
{
    for (int i = 0; i < _kerns.size(); i++)
	if ((code1 == CODE_ALL || _kerns[i].left == code1)
	    && (code2 == CODE_ALL || _kerns[i].right == code2))
	    _kerns[i].left = -1;
}

void
GsubEncoding::reencode_right_ligkern(int old_code, int new_code)
{
    for (int i = 0; i < _ligatures.size(); i++) {
	Ligature &l = _ligatures[i];
	for (int j = 1; j < l.in.size(); j++)
	    if (l.in[j] == old_code)
		l.in[j] = new_code;
    }
    for (int i = 0; i < _kerns.size(); i++) {
	Kern &k = _kerns[i];
	if (k.right == old_code)
	    k.right = new_code;
    }
}

int
GsubEncoding::twoligatures(int code1, Vector<int> &code2, Vector<int> &outcode, Vector<int> &context) const
{
    int n = 0;
    code2.clear();
    outcode.clear();
    context.clear();
    for (int i = 0; i < _ligatures.size(); i++) {
	const Ligature &l = _ligatures[i];
	if (l.in.size() == 2 && l.in[0] == code1 && l.in[1] >= 0 && l.out >= 0) {
	    code2.push_back(l.in[1]);
	    outcode.push_back(l.out);
	    context.push_back(l.context);
	    n++;
	}
    }
    return n;
}

int
GsubEncoding::kerns(int code1, Vector<int> &code2, Vector<int> &amount) const
{
    int n = 0;
    code2.clear();
    amount.clear();
    for (int i = 0; i < _kerns.size(); i++) {
	const Kern &k = _kerns[i];
	if (k.left == code1 && k.right >= 0) {
	    code2.push_back(k.right);
	    amount.push_back(k.amount);
	    n++;
	}
    }
    return n;
}

int
GsubEncoding::kern(int code1, int code2) const
{
    for (int i = 0; i < _kerns.size(); i++) {
	const Kern &k = _kerns[i];
	if (k.left == code1 && k.right == code2)
	    return k.amount;
    }
    return 0;
}


static PermString
unparse_glyphid(GsubEncoding::Glyph gid, const Vector<PermString> *gns)
{
    if (gid && gns && gns->size() > gid && (*gns)[gid])
	return (*gns)[gid];
    else if (gid == GsubEncoding::FAKE_LIGATURE)
	return "LIGATURE";
    else
	return permprintf("g%d", gid);
}

void
GsubEncoding::unparse(const Vector<PermString> *gns) const
{
    for (int c = 0; c < _encoding.size(); c++)
	if (Glyph g = _encoding[c]) {
	    fprintf(stderr, "%4x: ", c);
	    if (g != FAKE_LIGATURE)
		fprintf(stderr, "%s", unparse_glyphid(g, gns).cc());
	    else {
		for (int i = 0; i < _fake_ligatures.size(); i++)
		    if (_fake_ligatures[i].out == c) {
			const Ligature &l = _fake_ligatures[i];
			fprintf(stderr, " =");
			for (int j = 0; j < l.in.size(); j++)
			    fprintf(stderr, (j ? " %x/%s" : ":%x/%s"), l.in[0], unparse_glyphid(_encoding[l.in[j]], gns).cc());
		    }
	    }
	    for (int i = 0; i < _ligatures.size(); i++)
		if (_ligatures[i].in[0] == c) {
		    const Ligature &l = _ligatures[i];
		    fprintf(stderr, " + [");
		    for (int j = 1; j < l.in.size(); j++)
			fprintf(stderr, (j > 1 ? ",%x/%s" : "%x/%s"), l.in[j], unparse_glyphid(_encoding[l.in[j]], gns).cc());
		    fprintf(stderr, " => %x/%s]", l.out, unparse_glyphid(_encoding[l.out], gns).cc());
		}
	    fprintf(stderr, "\n");
	}
}
