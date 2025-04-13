local classData = EntityRegistry.ClassBuilder()

classData:On("init", function (self)
	local physSettings = {
		kind = "static",
		mass = 0.0,
		collider = BoxCollider3D.new(Vec3f(0.5)),
		objectLayer = Constants.ObjectLayerStatic
	}

	self:AddComponent("rigidbody3d", physSettings)

	self:SetInteractible(true)

	if CLIENT then
		self:SetInteractibleText("Pilot")

		local model = AssetLibrary.GetModel("computer")

		local gfx = self:AddComponent("graphics")
		gfx:AttachRenderable(model, Constants.RenderMask3D)
	end
end)

if SERVER then
	classData:On("interact", function (self, player)
		local computerNode = self:GetComponent("node")
		local exteriorEntity = self:GetEnvironment():GetExteriorShipEntity()

		player:PilotShip(exteriorEntity, computerNode:GetRotation())
	end)
end

EntityRegistry.RegisterClass("computer", classData)
