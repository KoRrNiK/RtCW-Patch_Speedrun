ghostPlayer
{
	nomipmaps
	nopicmip
	cull none
	{
		map *white
		rgbGen entity
		alphaGen entity
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		depthWrite
	}
}

speedrunWeaponTint
{
	nomipmaps
	nopicmip
	cull none
	{
		map *white
		rgbGen entity
		alphaGen entity
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		depthFunc equal
	}
}

speedrunWeaponFlat
{
	nomipmaps
	nopicmip
	cull none
	{
		map *white
		rgbGen entity
		alphaGen entity
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
	}
}

speedrunWeaponXray
{
	nomipmaps
	nopicmip
	cull none
	{
		map *white
		rgbGen entity
		alphaGen entity
		blendFunc GL_SRC_ALPHA GL_ONE
	}
}
