/*
 * Copyright 2011 Scientific Computation Research Center
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <PCU.h>
#include "apfMesh.h"
#include "apfNumbering.h"
#include "apfNumberingClass.h"
#include "apfShape.h"
#include "apfFieldData.h"
#include <sstream>
#include <fstream>
#include <cassert>
#include <cstdlib>
#include "lionBase64.h"

namespace apf {

class HasAll : public FieldOp
{
  public:
      virtual bool inEntity(MeshEntity* e)
      {
        if (!f->getData()->hasEntity(e))
          ok = false;
        return false;
      }
      bool run(FieldBase* f_)
      {
        f = f_;
        ok = true;
        this->apply(f);
        return ok;
      }
  private:
    bool ok;
    FieldBase* f;
};

static bool isPrintable(FieldBase* f)
{
  HasAll op;
  return op.run(f);
}

static bool isNodal(FieldBase* f)
{
  return f->getShape() == f->getMesh()->getShape();
}

static bool isIP(FieldBase* f)
{
  FieldShape* s = f->getShape();
  Mesh* m = f->getMesh();
  int d = m->getDimension();
  for (int i=0; i < d; ++i)
    if (s->hasNodesIn(i))
      return false;
  return s->hasNodesIn(d);
}

static void describeArray(
    std::ostream& file,
    const char* name,
    int type,
    int size,
    bool isWritingBinary = false )
{
  file << "type=\"";
  const char* typeNames[3] = {"Float64","Int32","Int64"};
  file << typeNames[type];
  file << "\" Name=\"" << name;
  if (isWritingBinary)
  {
    file << "\" format=\"binary\"";
  }
  else
  {
    file << "\" NumberOfComponents=\"" << size;
    file << "\" format=\"ascii\"";  
  }
}

static void writePDataArray(
    std::ostream& file,
    const char* name,
    int type,
    int size)
{
  file << "<PDataArray ";
  describeArray(file,name,type,size);
  file << "/>\n";
}

static void writePDataArray(
    std::ostream& file,
    FieldBase* f)
{
  file << "<PDataArray ";
  describeArray(file,
      f->getName(),
      f->getScalarType(),
      f->countComponents());
  file << "/>\n";
}

static void writePPoints(std::ostream& file, Field* f)
{
  file << "<PPoints>\n";
  writePDataArray(file,f);
  file << "</PPoints>\n";
}

static void writePPointData(std::ostream& file, Mesh* m)
{
  file << "<PPointData>\n";
  for (int i=0; i < m->countFields(); ++i)
  {
    Field* f = m->getField(i);
    if (isNodal(f) && isPrintable(f))
      writePDataArray(file,f);
  }
  for (int i=0; i < m->countNumberings(); ++i)
  {
    Numbering* n = m->getNumbering(i);
    if (isNodal(n) && isPrintable(n))
      writePDataArray(file,n);
  }
  for (int i=0; i < m->countGlobalNumberings(); ++i)
  {
    GlobalNumbering* n = m->getGlobalNumbering(i);
    if (isNodal(n) && isPrintable(n))
      writePDataArray(file,n);
  }
  file << "</PPointData>\n";
}

static int countIPs(FieldBase* f)
{
  /* assuming a non-mixed mesh here. for the already strained capabilities
     of VTK to accept IP fields, this is the best we can do */
  Mesh* m = f->getMesh();
  MeshIterator* it = m->begin(m->getDimension());
  MeshEntity* e = m->iterate(it);
  m->end(it);
  return f->countNodesOn(e);
}

static std::string getIPName(FieldBase* f, int point)
{
  std::stringstream ss;
  /* People looking at these files get scared of 0-based indexing
                                     V                        */
  ss << f->getName() << '_' << (point+1);
  return ss.str();
}

static void writeIP_PCellData(std::ostream& file, FieldBase* f)
{
  int n = countIPs(f);
  for (int p=0; p < n; ++p)
  {
    std::string s= getIPName(f,p);
    writePDataArray(file,s.c_str(),f->getScalarType(),f->countComponents());
  }
}

static void writePCellParts(std::ostream& file)
{
  writePDataArray(file, "apf_part", apf::Mesh::INT, 1);
}

static void writePCellData(std::ostream& file, Mesh* m)
{
  file << "<PCellData>\n";
  for (int i=0; i < m->countFields(); ++i)
  {
    Field* f = m->getField(i);
    if (isIP(f) && isPrintable(f))
      writeIP_PCellData(file,f);
  }
  for (int i=0; i < m->countNumberings(); ++i)
  {
    Numbering* n = m->getNumbering(i);
    if (isIP(n) && isPrintable(n))
      writeIP_PCellData(file,n);
  }
  for (int i=0; i < m->countGlobalNumberings(); ++i)
  {
    GlobalNumbering* n = m->getGlobalNumbering(i);
    if (isIP(n) && isPrintable(n))
      writeIP_PCellData(file,n);
  }
  writePCellParts(file);
  file << "</PCellData>\n";
}

static std::string getPieceFileName(const char* prefix, int id)
{
  std::stringstream ss;
  ss << prefix << id << ".vtu";
  return ss.str();
}

static std::string stripPath(std::string const& s)
{
  size_t i = s.rfind('/');
  if (i == std::string::npos)
    return s;
  return s.substr(i + 1, std::string::npos);
}

static void writePSources(std::ostream& file, const char* prefix)
{
  for (int i=0; i < PCU_Comm_Peers(); ++i)
  {
    std::string fileName = stripPath(getPieceFileName(prefix,i));
    file << "<Piece Source=\"" << fileName << "\"/>\n";
  }
}

static void writePvtuFile(const char* prefix, Mesh* m)
{
  std::string fileName = prefix;
  fileName += ".pvtu";
  std::ofstream file(fileName.c_str());
  assert(file.is_open());
  file << "<VTKFile type=\"PUnstructuredGrid\">\n";
  file << "<PUnstructuredGrid GhostLevel=\"0\">\n";
  writePPoints(file,m->getCoordinateField());
  writePPointData(file,m);
  writePCellData(file,m);
  writePSources(file,prefix);
  file << "</PUnstructuredGrid>\n";
  file << "</VTKFile>\n";
}

static void writeDataHeader(std::ostream& file, 
                            const char* name, 
                            int type, int size, 
                            bool isWritingBinary = false)
{
  file << "<DataArray ";
  describeArray(file,name,type,size,isWritingBinary);
  file << ">\n";
}

template <class T>
static void writeNodalField(std::ostream& file, 
                            FieldBase* f, 
                            DynamicArray<Node>& nodes,
                            bool isWritingBinary = false) 
{
  int nc = f->countComponents();
  writeDataHeader(file,f->getName(),f->getScalarType(),nc,isWritingBinary);
  NewArray<T> nodalData(nc);
  FieldDataOf<T>* data = static_cast<FieldDataOf<T>*>(f->getData());
  if (isWritingBinary)
  {
      unsigned int dataLen = nc * nodes.getSize();
      unsigned int dataLenBytes = dataLen*sizeof(T);
      T* dataToEncode = (T*)malloc(dataLenBytes);
      
      file << lion::base64Encode( (char*)&dataLenBytes, sizeof(dataLenBytes) );

      int dataIndex = 0;
      for ( size_t i = 0; i < nodes.getSize(); ++i )
      {
        data->getNodeComponents(nodes[i].entity,nodes[i].node,&(nodalData[0]));
        for ( int j = 0; j < nc; ++j )
        {
          dataToEncode[dataIndex] = nodalData[j];
          dataIndex++;
        }
      }
      file << lion::base64Encode( (char*)dataToEncode, dataLenBytes ) << '\n';
      free(dataToEncode);
  }
  else
  {
    for ( size_t i = 0; i < nodes.getSize(); ++i )
    {
      data->getNodeComponents(nodes[i].entity,nodes[i].node,&(nodalData[0]));
      for ( int j = 0; j < nc; ++j )
      {
        file << nodalData[j] << ' ';
      }
      file << '\n';
    }
  }
  file << "</DataArray>\n"; 
}

static void writePoints(std::ostream& file, 
                        Mesh* m, 
                        DynamicArray<Node>& nodes,
                        bool isWritingBinary = false)
{
  file << "<Points>\n";
  writeNodalField<double>(file,m->getCoordinateField(),nodes,isWritingBinary);
  file << "</Points>\n"; 
}

static int countElementNodes(Numbering* n, MeshEntity* e)
{
  return n->getShape()->getEntityShape(n->getMesh()->getType(e))->countNodes();
}

static void writeConnectivity(std::ostream& file, Numbering* n)
{
  file << "<DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n";
  Mesh* m = n->getMesh();
  MeshEntity* e;
  MeshIterator* elements = m->begin(m->getDimension());
  while ((e = m->iterate(elements)))
  {
    int nen = countElementNodes(n,e);
    NewArray<int> numbers(nen);
    getElementNumbers(n,e,numbers);
    for (int i=0; i < nen; ++i)
      file << numbers[i] << ' ';
    file << '\n';
  }
  m->end(elements);
  file << "</DataArray>\n";
}

static void writeOffsets(std::ostream& file, Numbering* n)
{
  file << "<DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
  Mesh* m = n->getMesh();
  MeshEntity* e;
  int o=0;
  MeshIterator* elements = m->begin(m->getDimension());
  while ((e = m->iterate(elements)))
  {
    o += countElementNodes(n,e);
    file << o << '\n';
  }
  m->end(elements);
  file << "</DataArray>\n";
}

static void writeTypes(std::ostream& file, Mesh* m)
{
  file << "<DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
  MeshEntity* e;
  int order = m->getShape()->getOrder();
  static int vtkTypes[Mesh::TYPES][2] =
  /* order
 linear,quadratic
      V  V */
   {{ 1,-1}//vertex
   ,{ 3,21}//edge
   ,{ 5,22}//triangle
   ,{ 9,23}//quad
   ,{10,24}//tet
   ,{12,25}//hex
   ,{13,-1}//prism
   ,{14,-1}//pyramid
   };
  MeshIterator* elements = m->begin(m->getDimension());
  while ((e = m->iterate(elements)))
    file << vtkTypes[m->getType(e)][order-1] << '\n';
  m->end(elements);
  file << "</DataArray>\n";
}

static void writeCells(std::ostream& file, Numbering* n)
{
  file << "<Cells>\n";
  writeConnectivity(file,n);
  writeOffsets(file,n);
  writeTypes(file,n->getMesh());
  file << "</Cells>\n"; 
}

static void writePointData(std::ostream& file, Mesh* m,
    DynamicArray<Node>& nodes)
{
  file << "<PointData>\n";
  for (int i=0; i < m->countFields(); ++i)
  {
    Field* f = m->getField(i);
    if (isNodal(f) && isPrintable(f))
      writeNodalField<double>(file,f,nodes);
  }
  for (int i=0; i < m->countNumberings(); ++i)
  {
    Numbering* n = m->getNumbering(i);
    if (isNodal(n) && isPrintable(n))
      writeNodalField<int>(file,n,nodes);
  }
  for (int i=0; i < m->countGlobalNumberings(); ++i)
  {
    GlobalNumbering* n = m->getGlobalNumbering(i);
    if (isNodal(n) && isPrintable(n))
      writeNodalField<long>(file,n,nodes);
  }
  file << "</PointData>\n";
}

template <class T>
class WriteIPField : public FieldOp
{
  public:
    int point;
    int components;
    NewArray<T> ipData;
    FieldDataOf<T>* data;
    MeshEntity* entity;
    std::ostream* fp;
    virtual bool inEntity(MeshEntity* e)
    {
      entity = e;
      return true;
    }
    virtual void atNode(int node)
    {
      if (node != point)
        return;
      data->getNodeComponents(entity,node,&(ipData[0]));
      for (int i=0; i < components; ++i)
        (*fp) << ipData[i] << ' ';
      (*fp) << '\n';
    }
    void runOnce(FieldBase* f)
    {
      std::string s = getIPName(f,point);
      components = f->countComponents();
      writeDataHeader(*fp,s.c_str(),f->getScalarType(),f->countComponents());
      ipData.allocate(components);
      data = static_cast<FieldDataOf<T>*>(f->getData());
      apply(f);
      (*fp) << "</DataArray>\n"; 
    }
    void run(std::ostream& file, FieldBase* f)
    {
      fp = &file;
      int n = countIPs(f);
      for (point=0; point < n; ++point)
        runOnce(f);
    }
};

static void writeCellParts( std::ostream& file, 
                            Mesh* m, bool 
                            isWritingBinary = false)
{
  writeDataHeader(file, "apf_part", apf::Mesh::INT, 1, isWritingBinary);
  size_t n = m->count(m->getDimension());
  int id = m->getId();

  if (isWritingBinary)
  {
    unsigned int dataLen = n;
    unsigned int dataLenBytes = dataLen*sizeof(int);
    int* dataToEncode = (int*)malloc(dataLenBytes);

    file << lion::base64Encode( (char*)&dataLenBytes, sizeof(dataLenBytes) );

    for (size_t i = 0; i < n; ++i )
    {
      dataToEncode[i] = id;
    }
    file << lion::base64Encode( (char*)dataToEncode, dataLenBytes ) << "\n";
    file << "</DataArray>\n";
    free(dataToEncode);
  }
  else
  { 
    for (size_t i = 0; i < n; ++i)
    {
      file << id << '\n';
    }
    file << "</DataArray>\n";
  }
}

static void writeCellData(std::ostream& file, 
                          Mesh* m, 
                          bool isWritingBinary = false)
{
  file << "<CellData>\n";
  WriteIPField<double> wd;
  for (int i=0; i < m->countFields(); ++i)
  {
    Field* f = m->getField(i);
    if (isIP(f) && isPrintable(f))
      wd.run(file,f);
  }
  WriteIPField<int> wi;
  for (int i=0; i < m->countNumberings(); ++i)
  {
    Numbering* n = m->getNumbering(i);
    if (isIP(n) && isPrintable(n))
      wi.run(file,n);
  }
  WriteIPField<long> wl;
  for (int i=0; i < m->countGlobalNumberings(); ++i)
  {
    GlobalNumbering* n = m->getGlobalNumbering(i);
    if (isIP(n) && isPrintable(n))
      wl.run(file,n);
  }
  writeCellParts(file, m, isWritingBinary);
  file << "</CellData>\n";
}

//function checks if the machine is a big or little endian machine
bool is_big_endian()
{
    union {
        unsigned int i;
        char c[4];
    } bint = {0x01020304};
    return bint.c[0] == 1; 
}

static void writeVtuFile( const char* prefix, 
                          Numbering* n, 
                          bool isWritingBinary = false)
{
  PCU_Barrier();
  double t0 = PCU_Time();
  std::string fileName = getPieceFileName(prefix,PCU_Comm_Self());
  std::stringstream buf;
  Mesh* m = n->getMesh();
  DynamicArray<Node> nodes;
  getNodes(n,nodes);
  buf << "<VTKFile type=\"UnstructuredGrid\"";
  if (isWritingBinary)
  {
    buf << " byte_order=";
    if (is_big_endian())
    {
      buf << "\"BigEndian\"";
    }
    else
    {
      buf << "\"LittleEndian\"";
    }
    buf << " header_type=\"UInt32\"";
  }
  buf<< ">\n";
  buf << "<UnstructuredGrid>\n";
  buf << "<Piece NumberOfPoints=\"" << nodes.getSize();
  buf << "\" NumberOfCells=\"" << m->count(m->getDimension());
  buf << "\">\n";
  writePoints(buf,m,nodes);
  writeCells(buf,n);
  writePointData(buf,m,nodes);
  writeCellData(buf,m,isWritingBinary);
  buf << "</Piece>\n";
  buf << "</UnstructuredGrid>\n";
  buf << "</VTKFile>\n";
  PCU_Barrier();
  double t1 = PCU_Time();
  if (!PCU_Comm_Self())
    printf("writeVtuFile into buffers: %f seconds\n", t1 - t0);
  {
    std::ofstream file(fileName.c_str());
    assert(file.is_open());
    file << buf.rdbuf();
  }
  PCU_Barrier();
  double t2 = PCU_Time();
  if (!PCU_Comm_Self())
    printf("writeVtuFile buffers to disk: %f seconds\n", t2 - t1);
}

void writeVtkFiles(const char* prefix, Mesh* m)
{
  double t0 = PCU_Time();
  if (!PCU_Comm_Self())
    writePvtuFile(prefix, m);
  Numbering* n = numberOverlapNodes(m,"apf_vtk_number");
  m->removeNumbering(n);
  writeVtuFile(prefix, n);
  double t1 = PCU_Time();
  if (!PCU_Comm_Self())
    printf("vtk files %s written in %f seconds\n",
        prefix, t1 - t0);
  delete n;
}

void writeOneVtkFile(const char* prefix, Mesh* m)
{
  /* creating a non-collective numbering is
     a tad bit risky, but we should be fine
     given the current state of the code */
  Numbering* n = numberOverlapNodes(m,"apf_vtk_number");
  m->removeNumbering(n);
  writeVtuFile(prefix, n);
  delete n;
}

void writeBinaryInlineVtkFiles(const char* prefix, Mesh* m)
{
  //*** this function passes a boolean argument to the functions telling them
  // to write binary vtu files ***
  bool isWritingBinary = true;

  double t0 = PCU_Time();
  if (!PCU_Comm_Self())
    writePvtuFile(prefix, m);
  Numbering* n = numberOverlapNodes(m,"apf_vtk_number");
  m->removeNumbering(n);
  writeVtuFile(prefix, n, isWritingBinary);
  double t1 = PCU_Time();
  if (!PCU_Comm_Self())
    printf("vtk files %s written in %f seconds\n",
        prefix, t1 - t0);
  delete n;
}

}
