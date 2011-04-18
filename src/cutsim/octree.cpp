/*  $Id$
 * 
 *  Copyright 2010-2011 Anders Wallin (anders.e.e.wallin "at" gmail.com)
 *  
 *  This file is part of OpenCAMlib.
 *
 *  OpenCAMlib is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  OpenCAMlib is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with OpenCAMlib.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <list>
// uncomment to disable assert() calls
// #define NDEBUG
#include <cassert>
#include <boost/foreach.hpp>

#include "point.h"
#include "triangle.h"
#include "numeric.h"
#include "octree.h"
#include "octnode.h"
#include "volume.h"

namespace ocl
{

//**************** Octree ********************/

Octree::Octree(double scale, unsigned int  depth, Point& centerp) {
    root_scale = scale;
    max_depth = depth;
                    // parent, idx, scale, depth
    root = new Octnode( NULL , 0, root_scale, 0 );
    root->center = new Point(centerp);
    for ( int n=0;n<8;++n) {
        root->child[n] = NULL;
    }
}

Octree::~Octree() {
    //std::cout << " ~Octree() \n";
    delete root;
    root = 0;
}

unsigned int Octree::get_max_depth() const {
    return max_depth;
}

double Octree::get_root_scale() const {
    return root_scale;
}

double Octree::leaf_scale() const {
    return (2.0*root_scale) / pow(2.0, (int)max_depth );
}
        
/// subdivide the Octree n times
void Octree::init(const unsigned int n) {
    for (unsigned int m=0;m<n;++m) {
        std::vector<Octnode*> nodelist;
        Octree::get_leaf_nodes(root, nodelist);
        BOOST_FOREACH( Octnode* node, nodelist) {
            node->subdivide();
        }
    }
}

void Octree::get_invalid_leaf_nodes( std::vector<Octnode*>& nodelist) const {
    get_invalid_leaf_nodes( root, nodelist );
}

void Octree::get_invalid_leaf_nodes(Octnode* current, std::vector<Octnode*>& nodelist) const {
    if ( current->childcount == 0 ) {
        if ( !current->valid() ) {
            nodelist.push_back( current );
        }
    } else {//surface()surface()
        for ( int n=0;n<8;++n) {
            if ( current->hasChild(n) ) {
                if ( !current->valid() ) {
                    get_leaf_nodes( current->child[n], nodelist );
                }
            }
        }
    }
}   


/// put leaf nodes into nodelist
void Octree::get_leaf_nodes(Octnode* current, std::vector<Octnode*>& nodelist) const {
    if ( current->isLeaf() ) {
        nodelist.push_back( current );
    } else {
        for ( int n=0;n<8;++n) {
            if ( current->child[n] != 0 )
                get_leaf_nodes( current->child[n], nodelist );
        }
    }
}

/// put all nodes into nodelist
void Octree::get_all_nodes(Octnode* current, std::vector<Octnode*>& nodelist) const {
    if ( current ) {
        nodelist.push_back( current );
        for ( int n=0;n<8;++n) {
            if ( current->child[n] != 0 )
                get_all_nodes( current->child[n], nodelist );
        }
    }
}

// subtract vol from the root
void Octree::diff_negative(const OCTVolume* vol) {
    diff_negative( this->root, vol);
}

// subtract vol from the Octnode curremt
void Octree::diff_negative(Octnode* current, const OCTVolume* vol) {
    current->evaluate( vol ); // this evaluates the distance field
                              // and sets the inside/outside flags
    if ( current->isLeaf() ) { // process only leaf-nodes
        if ( current->inside && !current->outside ) { 
            // inside nodes should be deleted
            Octnode* parent = current->parent;                          assert( parent );
            // if (parent) {
            if (debug) {
                double dist = ( *(current->center) -  Point(4,4,4)).norm();
                std::cout << " inside node, remove!: " << current->str() << " d= " << dist << " \n";
                if ( dist > 3.0 ) {
                    std::cout << " corners: \n";
                    for(int m=0;m<8;++m) {
                        double dist2 = ( *(current->vertex[m]) -  Point(7,7,7)).norm();
                        std::cout << *(current->vertex[m]) << " d= " << dist2 << " \n";
                    }
                    std::cout << " f-values: \n";
                    for(int m=0;m<8;++m) {
                        std::cout << current->f[m] << " ";
                    }
                    std::cout << "\n";
                }
                //assert( dist < 3.0 );
            }
            
            remove_node_vertices(current);
            
            unsigned int delete_index = current->idx;                   assert( delete_index >=0 && delete_index <=7 ); 
            delete parent->child[ delete_index ]; // call destructor!
            parent->child[ delete_index ]=0;
            --parent->childcount;
            assert( parent->childcount >=0 && parent->childcount <=8);
            if (parent->childcount == 0)  { // if the parent has become a leaf node
                //diff_negative(parent, vol); // this causes segfault...
                parent->evaluate( vol ); // back up the tree
                assert( parent->inside  ); // then it is itself inside
            }
            //}
        } else if (current->outside) {
            // do nothing to outside  leaf nodes.
            // this terminates recursion.
        } else {// these are intermediate leaf nodes
            if ( current->depth < (this->max_depth) ) { 
                // subdivide, if possible
                current->subdivide();                                   assert( current->childcount == 8 );
                for(int m=0;m<8;++m) {
                    assert(current->child[m]); // when we subdivide() there must be a child.
                    if ( vol->bb.overlaps( current->child[m]->bb ) )
                        diff_negative( current->child[m], vol); // call diff on child
                }
            } else { 
                // max depth reached, intermediate node, but can't subdivide anymore
            }
        }
    } else { // not a leaf, so go deeper into tree
        for(int m=0;m<8;++m) { 
                if ( current->child[m] ) {
                    if ( vol->bb.overlaps( current->child[m]->bb ) )
                        diff_negative( current->child[m], vol); // call diff on child
                }
        }
    }

}

// string repr
std::string Octree::str() const {
    std::ostringstream o;
    o << " Octree: ";
    std::vector<Octnode*> nodelist;
    Octree::get_all_nodes(root, nodelist);
    std::vector<int> nodelevel(this->max_depth);
    std::vector<int> invalidsAtLevel(this->max_depth);
    std::vector<int> surfaceAtLevel(this->max_depth);
    BOOST_FOREACH( Octnode* n, nodelist) {
        ++nodelevel[n->depth];
        if ( !n->valid() ) 
            ++invalidsAtLevel[n->depth];
        if (n->surface() ) 
            ++surfaceAtLevel[n->depth];
    }
    o << "  " << nodelist.size() << " leaf-nodes:\n";
    int m=0;
    BOOST_FOREACH( int count, nodelevel) {
        o << "depth="<<m <<"  " << count << " nodes, " << invalidsAtLevel[m] << " invalid, surface=" << surfaceAtLevel[m] << " \n";
        ++m;
    }
    return o.str();
}

} // end namespace
// end of file octree.cpp
