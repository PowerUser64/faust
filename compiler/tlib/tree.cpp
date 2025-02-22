/************************************************************************
 ************************************************************************
    FAUST compiler
    Copyright (C) 2003-2024 GRAME, Centre National de Creation Musicale
    ---------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ************************************************************************
 ************************************************************************/

/*****************************************************************************
******************************************************************************
                                TREE
                        Y. Orlarey, (c) Grame 2002
------------------------------------------------------------------------------
Trees are made of a Node associated with a list of branches : (Node x [CTree]).
Up to 4 branches are allowed in this implementation. A hash table is used to
maximize the sharing of trees during construction : trees at different
addresses always have a different content. Reference counting is used for
garbage collection, and smart pointers P<CTree> should be used for permanent
storage of trees.

 API:
 ----
 tree (n)            : tree of node n with no branch
 tree (n, t1)        : tree of node n with a branch t
 tree (n, t1,...,tm) : tree of node n with m branches t1,...,tm

 Pattern matching :

 if (isTree (t, n))           : t has node n and no branches;
 if (isTree (t, n, &t1)       : t has node n and 1 branch, t1 is set accordingly;
 if (isTree (t, n, &t1...&tm) : t has node n and m branches, ti's are set accordingly;

 Accessors :

 t->node()    : the node of t { return fNode; }
 t->arity()   : the number of branches of t { return fArity; }
 t->branch(i) : the ith branch of t

 Attributs :

 t->attribut()   : return the attribute (also a tree) of t
 t->attribut(t') : set the attribute of t to t'

 Warning :
 ---------
 Since reference counters are used for garbage collecting, one must be careful not to
 create cycles in trees. The only possible source of cycles is by setting the attribute
 of a tree t to a tree t' that contains t as a subtree.

 Properties:
 -----------
    If p and q are two CTree pointers :
        p != q <=> *p != *q

 History :
 ---------
    2002-02-08 : First version
    2002-10-14 : counts for height and recursiveness added

******************************************************************************
*****************************************************************************/

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cstdlib>
#include <fstream>

#include "exception.hh"
#include "global.hh"
#include "tree.hh"

using namespace std;

#ifdef WIN32
#pragma warning(disable : 4800)
#endif

#define ERROR(s, t)                        \
    {                                      \
        stringstream error;                \
        error << s << *t << endl;          \
        throw faustexception(error.str()); \
    }

Tree         CTreeBase::gHashTable[kHashTableSize];
bool         CTreeBase::gDetails       = false;
unsigned int CTreeBase::gVisitTime     = 0;
size_t       CTreeBase::gSerialCounter = 0;

int               CDTree::kBlockSize      = global::getDebug("FAUST_DTREE_SIZE") ?: 1024;
Tree              CDTree::gAllocatedBlock = nullptr;
std::vector<Tree> CDTree::gAllocatedBlocks;

// Constructor : add the tree to the hash table
CTreeBase::CTreeBase(size_t hk, const Node& n, const tvec& br)
    : fNode(n),
      fType(0),
      fHashKey(hk),
      fSerial(++gSerialCounter),
      fAperture(calcTreeAperture(n, br)),
      fVisitTime(0),
      fBranch(br)
{
    // link in the hash table
    int j         = hk % kHashTableSize;
    fNext         = gHashTable[j];
    gHashTable[j] = this;
}

// Destructor
CTreeBase::~CTreeBase()
{
    /*
     Remove the tree from the hash table is not needed
     since all pointers are either managed using the Garbageable model
     or with CDTree "successive pointers" allocation model.
     */
}

// equivalence
bool CTreeBase::equiv(const Node& n, const tvec& br) const
{
    return (fNode == n) && (fBranch == br);
}

size_t CTreeBase::calcTreeHash(const Node& n, const tvec& br)
{
    size_t hk = std::hash<void*>()(n.getPointer());
    for (const auto& ptr : br) {
        // Taken from by boost::hash_combine
        hk = hk ^ (ptr->fHashKey + 0x9e3779b9 + (hk << 6) + (hk >> 2));
    }
    return hk;
}

Tree CTreeBase::make(const Node& n, int ar, Tree* tbl)
{
    vector<Tree> br(tbl, tbl + ar);
    return CTreeBase::make(n, br);
}

Tree CTreeBase::make(const Node& n, const tvec& br)
{
    size_t hk = calcTreeHash(n, br);
    Tree   t  = gHashTable[hk % kHashTableSize];

    while (t && !t->equiv(n, br)) {
        t = t->fNext;
    }

    if (t) {
        return t;
    } else if (global::isDebug("FAUST_DTREE")) {
        return new CDTree(hk, n, br);
    } else {
        return new CTree(hk, n, br);
    }
}

ostream& CTreeBase::print(ostream& fout) const
{
    if (gDetails) {
        // print the adresse of the tree
        fout << "<" << this << ">@";
    }
    fout << node();
    int a = arity();
    if (a > 0) {
        int  i;
        char sep;
        for (sep = '[', i = 0; i < a; sep = ',', i++) {
            fout << sep;
            branch(i)->print(fout);
        }
        fout << ']';
    }

    return fout;
}

void CTreeBase::control()
{
    printf("\ngHashTable Content :\n\n");
    for (int i = 0; i < kHashTableSize; i++) {
        Tree t = gHashTable[i];
        if (t) {
            printf("%4d = ", i);
            while (t) {
                /*t->print();*/
                printf(" => ");
                t = t->fNext;
            }
            printf("VOID\n");
        }
    }
    printf("\nEnd gHashTable\n");
}

void CTreeBase::init()
{
    gSerialCounter = 0;
    gVisitTime     = 0;
    gDetails       = false;
    memset(gHashTable, 0, sizeof(Tree) * kHashTableSize);
}

void CDTree::init()
{
    gAllocatedBlock = nullptr;
    gSerialCounter  = 0;
    gVisitTime      = 0;
    gDetails        = false;
    memset(gHashTable, 0, sizeof(Tree) * kHashTableSize);
}

void CDTree::cleanup()
{
    for (const auto& it : gAllocatedBlocks) {
        delete[] it;
    }
    gAllocatedBlocks.clear();
}

// if t has a node of type int, return it, or float, return casted to int, otherwise error
LIBFAUST_API int tree2int(Tree t)
{
    double x;
    int    i;

    if (isInt(t->node(), &i)) {
        // nothing to do
    } else if (isDouble(t->node(), &x)) {
        i = int(x);
    } else {
        ERROR("ERROR : the parameter must be an integer constant numerical expression : ", t);
    }
    return i;
}

// if t has a node of type int, return casted to double, or double, return it, otherwise error
LIBFAUST_API double tree2double(Tree t)
{
    double x;
    int    i;

    if (isInt(t->node(), &i)) {
        x = double(i);
    } else if (isDouble(t->node(), &x)) {
        // nothing to do
    } else {
        ERROR("ERROR : the parameter must be a real constant numerical expression : ", t);
    }
    return x;
}

// if t has a node of type symbol, return its name otherwise error
LIBFAUST_API const char* tree2str(Tree t)
{
    Sym s;
    if (!isSym(t->node(), &s)) {
        ERROR("ERROR : the parameter must be a symbol known at compile time : ", t);
    }
    return name(s);
}

string tree2quotedstr(Tree t)
{
    return "\"" + string(tree2str(t)) + "\"";
}

// if t has a node of type ptr, return it otherwise error
void* tree2ptr(Tree t)
{
    void* x;
    if (!isPointer(t->node(), &x)) {
        ERROR("ERROR : the parameter must be a pointer known at compile time : ", t);
    }
    return x;
}

/*
bool isTree (const Tree& t, const Node& n)
{
    return (t->node() == n) && (t->arity() == 0);
}
*/

// If it's not a problem, it's more practical
bool isTree(const Tree& t, const Node& n)
{
    return (t->node() == n);
}

bool isTree(const Tree& t, const Node& n, Tree& a)
{
    if ((t->node() == n) && (t->arity() == 1)) {
        a = t->branch(0);
        return true;
    } else {
        return false;
    }
}

bool isTree(const Tree& t, const Node& n, Tree& a, Tree& b)
{
    if ((t->node() == n) && (t->arity() == 2)) {
        a = t->branch(0);
        b = t->branch(1);
        return true;
    } else {
        return false;
    }
}

bool isTree(const Tree& t, const Node& n, Tree& a, Tree& b, Tree& c)
{
    if ((t->node() == n) && (t->arity() == 3)) {
        a = t->branch(0);
        b = t->branch(1);
        c = t->branch(2);
        return true;
    } else {
        return false;
    }
}

bool isTree(const Tree& t, const Node& n, Tree& a, Tree& b, Tree& c, Tree& d)
{
    if ((t->node() == n) && (t->arity() == 4)) {
        a = t->branch(0);
        b = t->branch(1);
        c = t->branch(2);
        d = t->branch(3);
        return true;
    } else {
        return false;
    }
}

bool isTree(const Tree& t, const Node& n, Tree& a, Tree& b, Tree& c, Tree& d, Tree& e)
{
    if ((t->node() == n) && (t->arity() == 5)) {
        a = t->branch(0);
        b = t->branch(1);
        c = t->branch(2);
        d = t->branch(3);
        e = t->branch(4);
        return true;
    } else {
        return false;
    }
}

// Support for symbol user data
LIBFAUST_API void* getUserData(Tree t)
{
    Sym s;
    if (isSym(t->node(), &s)) {
        return getUserData(s);
    } else {
        return nullptr;
    }
}

/**
 * export the properties of a CTree as two vectors, one for the keys
 * and one for the associated values
 */
void CTreeBase::exportProperties(vector<Tree>& keys, vector<Tree>& values)
{
    for (const auto& it : fProperties) {
        keys.push_back(it.first);
        values.push_back(it.second);
    }
}
