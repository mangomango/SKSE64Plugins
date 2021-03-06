#include "CDXD3DDevice.h"
#include "CDXMesh.h"
#include "CDXShader.h"
#include "CDXCamera.h"
#include "CDXMaterial.h"
#include "CDXPicker.h"

using namespace DirectX;

CDXMesh::CDXMesh()
{
	m_vertexBuffer = nullptr;
	m_vertCount = 0;
	m_indexBuffer = nullptr;
	m_indexCount = 0;
	m_visible = true;
	m_material = nullptr;
	m_transform = XMMatrixIdentity();
	m_topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

CDXMesh::~CDXMesh()
{
	
}

void CDXMesh::Release()
{
	m_vertCount = 0;
	m_indexCount = 0;
	m_visible = false;
	if(m_vertexBuffer) {
		m_vertexBuffer->Release();
		m_vertexBuffer = nullptr;
	}
	if(m_indexBuffer) {
		m_indexBuffer->Release();
		m_indexBuffer = nullptr;
	}
	if(m_material) {
		m_material->Release();
		delete m_material;
		m_material = nullptr;
	}

	// Release the arrays now that the vertex and index buffers have been created and loaded.
	delete[] m_vertices;
	m_vertices = 0;

	delete[] m_indices;
	m_indices = 0;
}

void CDXMesh::SetMaterial(CDXMaterial * material)
{
#ifdef CDX_MUTEX
	std::lock_guard<std::mutex> guard(m_mutex);
#endif
	m_material = material;
}

CDXMaterial * CDXMesh::GetMaterial()
{
#ifdef CDX_MUTEX
	std::lock_guard<std::mutex> guard(m_mutex);
#endif
	return m_material;
}

void CDXMesh::SetVisible(bool visible)
{
#ifdef CDX_MUTEX
	std::lock_guard<std::mutex> guard(m_mutex);
#endif
	m_visible = visible;
}
bool CDXMesh::IsVisible()
{
#ifdef CDX_MUTEX
	std::lock_guard<std::mutex> guard(m_mutex);
#endif
	return m_visible;
}

ID3D11Buffer * CDXMesh::GetVertexBuffer()
{
#ifdef CDX_MUTEX
	std::lock_guard<std::mutex> guard(m_mutex);
#endif
	return m_vertexBuffer;
}
ID3D11Buffer * CDXMesh::GetIndexBuffer()
{
#ifdef CDX_MUTEX
	std::lock_guard<std::mutex> guard(m_mutex);
#endif
	return m_indexBuffer;
}

UInt32 CDXMesh::GetIndexCount()
{
	return m_indexCount;
}

UInt32 CDXMesh::GetFaceCount()
{
	return m_indexCount / 3;
}

UInt32 CDXMesh::GetVertexCount()
{
	return m_vertCount;
}

XMVECTOR CalculateFaceNormal(UInt32 f, CDXMeshIndex * faces, CDXMeshVert * vertices)
{
	XMVECTOR vNormal;
	CDXMeshVert * v1 = &vertices[faces[f]];
	CDXMeshVert * v2 = &vertices[faces[f + 1]];
	CDXMeshVert * v3 = &vertices[faces[f + 2]];

	auto e1 = XMVectorSubtract(XMLoadFloat3(&v2->Position), XMLoadFloat3(&v1->Position));
	auto e2 = XMVectorSubtract(XMLoadFloat3(&v3->Position), XMLoadFloat3(&v2->Position));

	vNormal = XMVector3Cross(e1, e2);
	vNormal = XMVector3Normalize(vNormal);
	return vNormal;
}

bool IntersectSphere(float radius, float & dist, CDXVec & center, CDXVec & rayOrigin, CDXVec & rayDir)
{
	//FLOAT t0 = -1, t1 = -1; // solutions for t if the ray intersects

	CDXVec L = XMVectorSubtract(center, rayOrigin);
	float tca = XMVectorGetX(XMVector3Dot(L, rayDir));
	if (tca < 0) return false;
	float d2 = XMVector3Dot(L, L).m128_f32[0] - tca * tca;
	if (d2 > radius) return false;
	float thc = sqrt(radius - d2);
	//t0 = tca - thc;
	//t1 = tca + thc;
	dist = d2;
	return true;
}

bool IntersectTriangle( const CDXVec& orig, const CDXVec& dir, CDXVec& v0, CDXVec& v1, CDXVec& v2, float* t, float* u, float* v )
{
	// Find vectors for two edges sharing vert0
	CDXVec edge1 = XMVectorSubtract(v1, v0);
	CDXVec edge2 = XMVectorSubtract(v2, v0);

	// Begin calculating determinant - also used to calculate U parameter
	CDXVec pvec = XMVector3Cross(dir, edge2);

	// If determinant is near zero, ray lies in plane of triangle
	float det = XMVectorGetX(XMVector3Dot(edge1, pvec));

	CDXVec tvec;
	if(det > 0) {
		tvec = XMVectorSubtract(orig, v0);
	} else {
		tvec = XMVectorSubtract(v0, orig);
		det = -det;
	}

	if(det < 0.0001f)
		return false;

	// Calculate U parameter and test bounds
	*u = XMVectorGetX(XMVector3Dot(tvec, pvec));
	if(*u < 0.0f || *u > det)
		return false;

	// Prepare to test V parameter
	CDXVec qvec = XMVector3Cross(tvec, edge1);

	// Calculate V parameter and test bounds
	*v = XMVectorGetX(XMVector3Dot(dir, qvec));
	if(*v < 0.0f || *u + *v > det)
		return false;

	// Calculate t, scale parameters, ray intersects triangle
	*t = XMVectorGetX(XMVector3Dot(edge2, qvec));
	float fInvDet = 1.0f / det;
	*t *= fInvDet;
	*u *= fInvDet;
	*v *= fInvDet;

	return true;
}

CDXMeshVert * CDXMesh::LockVertices(const LockMode type)
{
	if (type == LockMode::WRITE)
	{
		D3D11_MAPPED_SUBRESOURCE vertResource;
		CDXMeshVert* pVertices = nullptr;
		HRESULT res = m_pDevice->GetDeviceContext()->Map(m_vertexBuffer, 0, (D3D11_MAP)type, 0, &vertResource);
		if (res == S_OK)
		{
			return static_cast<CDXMeshVert*>(vertResource.pData);
		}
	}

	return m_vertices;
}

CDXMeshIndex * CDXMesh::LockIndices()
{
	return m_indices;
}

void CDXMesh::UnlockVertices(const LockMode type)
{
	if (type == LockMode::WRITE)
	{
		m_pDevice->GetDeviceContext()->Unmap(m_vertexBuffer, 0);
	}
}

void CDXMesh::UnlockIndices(bool write)
{
	if(write)
		m_pDevice->GetDeviceContext()->UpdateSubresource(m_indexBuffer, 0, nullptr, m_indices, 0, 0);
}

bool CDXMesh::Pick(CDXRayInfo & rayInfo, CDXPickInfo & pickInfo)
{
#ifdef CDX_MUTEX
	std::lock_guard<std::mutex> guard(m_mutex);
#endif
	CDXMeshVert* pVertices = LockVertices(LockMode::READ);
	CDXMeshIndex* pIndices = LockIndices();

	if (!pVertices || !pIndices)
		return false;

	float hitDist = FLT_MAX;
	CDXVec hitNormal = XMVectorZero();

	// Edges = Face * 3
	for(UInt32 e = 0; e < m_indexCount; e += 3)
	{
		CDXVec v0 = XMLoadFloat3(&pVertices[pIndices[e + 0]].Position);
		CDXVec v1 = XMLoadFloat3(&pVertices[pIndices[e + 1]].Position);
		CDXVec v2 = XMLoadFloat3(&pVertices[pIndices[e + 2]].Position);

		// Calculate the norm of the face
		CDXVec fNormal = XMVectorZero();
		CDXVec f1 = XMVectorSubtract(v1, v0);
		CDXVec f2 = XMVectorSubtract(v2, v1);
		fNormal = XMVector3Cross(f1, f2);
		fNormal = XMVector3Normalize(fNormal);

		// Normalize the direction, just in case
		CDXVec vDir = XMVector3Normalize(rayInfo.direction);

		// Skip faces that are in the same direction as the ray
		if(XMVectorGetX(XMVector3Dot(vDir, fNormal)) >= 0)
			continue;

		// Skip face that doesn't intersect with the ray
		float fDist = -1;
		float fBary1 = 0;
		float fBary2 = 0;
		if (!IntersectTriangle(rayInfo.origin, rayInfo.direction, v0, v1, v2, &fDist, &fBary1, &fBary2))
			continue;

		if (fDist < hitDist) {
			hitDist = fDist;
			hitNormal = fNormal;
		}
	}

	UnlockVertices(LockMode::READ);
	UnlockIndices();

	pickInfo.ray = rayInfo;
	pickInfo.dist = hitDist;

	if (hitDist != FLT_MAX) {
		CDXVec hitVec = XMVectorReplicate(hitDist);
		CDXVec vHit = XMVectorMultiplyAdd(rayInfo.direction, hitVec, rayInfo.origin);
		pickInfo.origin = XMVectorSetW(vHit, 0.0f);
		pickInfo.normal = hitNormal;
		pickInfo.isHit = true;
	}
	else {
		pickInfo.origin = XMVectorZero();
		pickInfo.normal = XMVectorZero();
		pickInfo.isHit = false;
	}

	return pickInfo.isHit;
}

bool CDXMesh::InitializeBuffers(CDXD3DDevice * device, UInt32 vertexCount, UInt32 indexCount, std::function<void(CDXMeshVert*, CDXMeshIndex*)> fillFunction)
{
	D3D11_BUFFER_DESC vertexBufferDesc, indexBufferDesc;
	D3D11_SUBRESOURCE_DATA vertexData, indexData;
	HRESULT result;

	m_pDevice = device;
	m_vertCount = vertexCount;
	m_indexCount = indexCount;

	auto pDevice = device->GetDevice();
	auto pDeviceContext = device->GetDeviceContext();

	// Create the vertex array.
	m_vertices = new CDXMeshVert[m_vertCount];
	if (!m_vertices)
	{
		return false;
	}

	// Create the index array.
	m_indices = new CDXMeshIndex[m_indexCount];
	if (!m_indices)
	{
		return false;
	}

	// Load the vertex array and index array with data.
	fillFunction(m_vertices, m_indices);
	
	// Set up the description of the static vertex buffer.
	vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	vertexBufferDesc.ByteWidth = sizeof(CDXMeshVert) * m_vertCount;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vertexBufferDesc.MiscFlags = 0;
	vertexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the vertex data.
	vertexData.pSysMem = m_vertices;
	vertexData.SysMemPitch = 0;
	vertexData.SysMemSlicePitch = 0;

	// Now create the vertex buffer.
	result = pDevice->CreateBuffer(&vertexBufferDesc, &vertexData, &m_vertexBuffer);
	if (FAILED(result))
	{
		return false;
	}

	// Set up the description of the static index buffer.
	indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	indexBufferDesc.ByteWidth = sizeof(CDXMeshIndex) * m_indexCount;
	indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	indexBufferDesc.CPUAccessFlags = 0;
	indexBufferDesc.MiscFlags = 0;
	indexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the index data.
	indexData.pSysMem = m_indices;
	indexData.SysMemPitch = 0;
	indexData.SysMemSlicePitch = 0;

	// Create the index buffer.
	result = pDevice->CreateBuffer(&indexBufferDesc, &indexData, &m_indexBuffer);
	if (FAILED(result))
	{
		return false;
	}

	return true;
}

void CDXMesh::Render(CDXD3DDevice * device, CDXShader * shader)
{
#ifdef CDX_MUTEX
	std::lock_guard<std::mutex> guard(m_mutex);
#endif
	unsigned int stride;
	unsigned int offset;

	// Set vertex buffer stride and offset.
	stride = sizeof(CDXMeshVert);
	offset = 0;

	auto pDeviceContext = device->GetDeviceContext();

	// Set the vertex buffer to active in the input assembler so it can be rendered.
	pDeviceContext->IASetVertexBuffers(0, 1, &m_vertexBuffer, &stride, &offset);

	// Set the index buffer to active in the input assembler so it can be rendered.
	pDeviceContext->IASetIndexBuffer(m_indexBuffer, DXGI_FORMAT_R16_UINT, 0);

	// Set the type of primitive that should be rendered from this vertex buffer, in this case triangles.
	pDeviceContext->IASetPrimitiveTopology(m_topology);

	shader->RenderShader(device, m_material);

	// Render the triangle.
	pDeviceContext->DrawIndexed(m_indexCount, 0, 0);
}
