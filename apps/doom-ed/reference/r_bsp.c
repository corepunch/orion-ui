#include <OpenGL/gl3.h>
#include <cglm/cglm.h>
#include <cglm/struct.h>
#include <stdbool.h>
#include "../mapview/map.h"

typedef unsigned angle_t;

int      sscount;
int      linecount;
int      loopcount;

int        viewwidth;
float      viewx;
float      viewy;
float      viewz;
angle_t    viewangle;

//
// ClipWallSegment
// Clips the given range of columns
// and includes it in the new clip list.
//
typedef  struct
{
  int  first;
  int last;
  
} cliprange_t;

#define MAXSEGS    32

// newend is one past the last valid seg
cliprange_t*  newend;
cliprange_t  solidsegs[MAXSEGS];

#define FINEANGLES    8192
#define FINEMASK    (FINEANGLES-1)

// 0x100000000 to 0x2000
#define ANGLETOFINESHIFT  19

angle_t      clipangle;

// The viewangletox[viewangle + FINEANGLES/4] lookup
// maps the visible view angles to screen X coordinates,
// flattening the arc to a flat projection plane.
// There will be many angles mapped to the same X.
int      viewangletox[FINEANGLES/2];


#define ANGLE_MAX 0x100000000u  // 2^32
#define PI 3.14159265358979323846
#define TWO_PI (2.0 * PI)
// Binary Angle Measument, BAM.
#define ANG45      0x20000000
#define ANG90      0x40000000
#define ANG180    0x80000000
#define ANG270    0xc0000000


angle_t rad2angle(float angle) {
  if (angle < 0)
    angle += TWO_PI;           // normalize to [0, 2PI)
  
  // Convert from radians to angle_t (0..0xFFFFFFFF)
  return (angle_t)(angle * (ANGLE_MAX / TWO_PI));
}

angle_t R_PointToAngle(float x1, float y1, float x2, float y2) {
  float dx = x2 - x1;
  float dy = y2 - y1;
  
  return rad2angle(atan2f(dy, dx));  // radians, range [-PI, PI]
}

//#define MAXSEGS    32
//
//// newend is one past the last valid seg
//cliprange_t*  newend;
//cliprange_t  solidsegs[MAXSEGS];

maplinedef_t*    curline;
mapsidedef_t*    sidedef;
maplinedef_t*    linedef;
mapsector_t*  frontsector;
mapsector_t*  backsector;

enum
{
  BOXTOP,
  BOXBOTTOM,
  BOXLEFT,
  BOXRIGHT
};  // bbox coordinates

//
// R_CheckBBox
// Checks BSP node/subtree bounding box.
// Returns true
//  if some part of the bbox might be visible.
//
int  checkcoord[12][4] =
{
  {3,0,2,1},
  {3,0,2,0},
  {3,1,2,0},
  {0},
  {2,0,2,1},
  {0,0,0,0},
  {3,1,3,0},
  {0},
  {2,0,3,1},
  {2,1,3,1},
  {2,1,3,0}
};

//
// R_ClearClipSegs
//
void R_ClearClipSegs (int width)
{
  viewwidth = width;
  solidsegs[0].first = -(0x7fffffff);
  solidsegs[0].last = -1;
  solidsegs[1].first = viewwidth;
  solidsegs[1].last = 0x7fffffff;
  newend = solidsegs+2;
}

bool R_CheckBBox (int16_t*  bspcoord)
{
  int      boxx;
  int      boxy;
  int      boxpos;
  
  float    x1;
  float    y1;
  float    x2;
  float    y2;
  
  angle_t    angle1;
  angle_t    angle2;
  angle_t    span;
  angle_t    tspan;
  
  cliprange_t*  start;
  
  int      sx1;
  int      sx2;
  
  // Find the corners of the box
  // that define the edges from current viewpoint.
  if (viewx <= bspcoord[BOXLEFT])
    boxx = 0;
  else if (viewx < bspcoord[BOXRIGHT])
    boxx = 1;
  else
    boxx = 2;
  
  if (viewy >= bspcoord[BOXTOP])
    boxy = 0;
  else if (viewy > bspcoord[BOXBOTTOM])
    boxy = 1;
  else
    boxy = 2;
  
  boxpos = (boxy<<2)+boxx;
  if (boxpos == 5)
    return true;
  
  x1 = bspcoord[checkcoord[boxpos][0]];
  y1 = bspcoord[checkcoord[boxpos][1]];
  x2 = bspcoord[checkcoord[boxpos][2]];
  y2 = bspcoord[checkcoord[boxpos][3]];
  
  // check clip list for an open space
  angle1 = R_PointToAngle (viewx, viewy, x1, y1) - viewangle;
  angle2 = R_PointToAngle (viewx, viewy, x2, y2) - viewangle;
  
  span = angle1 - angle2;
  
  // Sitting on a line?
  if (span >= ANG180)
    return true;
  
  tspan = angle1 + clipangle;
  
  if (tspan > 2*clipangle)
  {
    tspan -= 2*clipangle;
    
    // Totally off the left edge?
    if (tspan >= span)
      return false;
    
    angle1 = clipangle;
  }
  tspan = clipangle - angle2;
  if (tspan > 2*clipangle)
  {
    tspan -= 2*clipangle;
    
    // Totally off the left edge?
    if (tspan >= span)
      return false;
    
    angle2 = (clipangle * -1);
  }
  
  // Find the first clippost
  //  that touches the source post
  //  (adjacent pixels are touching).
  sx1 = viewangletox[(angle1+ANG90)>>ANGLETOFINESHIFT];
  sx2 = viewangletox[(angle2+ANG90)>>ANGLETOFINESHIFT];
  
  // Does not cross a pixel.
  if (sx1 == sx2)
    return false;
  sx2--;
  
  start = solidsegs;
  while (start->last < sx2)
    start++;
  
  if (sx1 >= start->first
      && sx2 <= start->last)
  {
    // The clippost contains the new span.
    return false;
  }
  
  return true;
}

int R_PointOnSide(float x, float y, mapnode_t* node) {
  float  dx;
  float  dy;
  float  left;
  float  right;
  
  if (!node->dx)
  {
    if (x <= node->x)
      return node->dy > 0;
    
    return node->dy < 0;
  }
  if (!node->dy)
  {
    if (y <= node->y)
      return node->dx < 0;
    
    return node->dx > 0;
  }
  
  dx = (x - node->x);
  dy = (y - node->y);
  
  left = node->dy * dx;
  right = dy * node->dx;
  
  if (right < left)
  {
    // front side
    return 0;
  }
  // back side
  return 1;
}

//
// R_Subsector
// Determine floor/ceiling planes.
// Add sprites of things in sector.
// Draw one or more line segments.
//
void R_Subsector (map_data_t *map, int num, viewdef_t *viewdef)
{
  int count/*, sector*/;
  mapseg_t* line;
  
#ifdef RANGECHECK
  if (num >= numsubsectors)
    I_Error ("R_Subsector: ss %i with numss = %i", num, numsubsectors);
#endif
  
  sscount++;
  mapsubsector_t* sub = &map->subsectors[num];
  mapseg_t* seg = &map->segs[sub->firstseg];

  frontsector = &map->sectors[map->sidedefs[map->linedefs[seg->linedef].sidenum[seg->side]].sector];
  count = sub->numsegs;
  line = &map->segs[sub->firstseg];
  
//  if (frontsector->floorheight < viewz)
//  {
//    floorplane = R_FindPlane (frontsector->floorheight, frontsector->floorpic, frontsector->lightlevel);
//  }
//  else
//    floorplane = NULL;
//  
//  if (frontsector->ceilingheight > viewz || frontsector->ceilingpic == skyflatnum)
//  {
//    ceilingplane = R_FindPlane (frontsector->ceilingheight, frontsector->ceilingpic, frontsector->lightlevel);
//  }
//  else
//    ceilingplane = NULL;
  
  if (map->floors.sectors[frontsector - map->sectors].frame != viewdef->frame)
  {
//    sorted_flats[sorted_flats_count++] = planes + sector;
    map->floors.sectors[frontsector - map->sectors].frame = viewdef->frame;
  }
  
//  R_AddSprites (frontsector);
  
  while (count--)
  {
//    R_AddLine (line);
    line++;
  }
}


// RenderBSPNode
// Renders all subsectors below a given node,
//  traversing subtree recursively.
// Just call with BSP root.
void R_RenderBSPNode (map_data_t *map, int bspnum, viewdef_t *viewdef)
{
  mapnode_t*  bsp;
  int    side;
  
  
  viewx = viewdef->player.x;
  viewy = viewdef->player.y;
  viewz = viewdef->player.z;
  viewangle = rad2angle(viewdef->player.angle);
  
  // Found a subsector?
  if (bspnum & NF_SUBSECTOR)
  {
    if (bspnum == -1)
      R_Subsector (map, 0, viewdef);
    else
      R_Subsector (map, bspnum&(~NF_SUBSECTOR), viewdef);
    return;
  }
  
  bsp = &map->nodes[bspnum];
  
  // Decide which side the view point is on.
  side = R_PointOnSide(viewx, viewy, bsp);
  
  // Recursively divide front space.
  R_RenderBSPNode (map, bsp->children[side], viewdef);
  
  // Possibly divide back space.
  if (R_CheckBBox (bsp->bbox[side^1]))
    R_RenderBSPNode (map, bsp->children[side^1], viewdef);
}

