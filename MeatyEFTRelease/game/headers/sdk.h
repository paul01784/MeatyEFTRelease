#pragma once
#include <cstdint>

namespace sdk
{
	namespace GameWorld
	{
		constexpr uint64_t Location = 0xD0;
		
	}

	namespace ClientLocalGameWorld
	{
		constexpr uint64_t LootList = 0x198;
		constexpr uint64_t RegisteredPlayers = 0x1B8;
		constexpr uint64_t MainPlayer = 0x210;
		constexpr uint64_t Grenades = 0x288;
		constexpr uint64_t ExfiltrationController = 0x58;
		constexpr uint64_t SynchronizableObjectLogicProcessor = 0x248; // object
		constexpr uint64_t btrController = 0x28;
	}

	namespace ExfiltrationController
	{
		constexpr uint64_t ExfiltrationPoints = 0x20; // EFT.Interactive.ExfiltrationPoint[]
		constexpr uint64_t ScavExfiltrationPoints = 0x28; // EFT.Interactive.ScavExfiltrationPoint[]
		constexpr uint64_t MSecretExfiltrationPoints = 0x30; // EFT.Interactive.SecretExfiltrationPoint[]
	}

	namespace ExfiltrationPoint
	{
		constexpr uint64_t Status = 0x58; // EExfiltrationStatus
		constexpr uint64_t Settings = 0x98; // ExitTriggerSettings
		constexpr uint64_t EligibleEntryPoints = 0xC0; // String[] — PMC spawn entry points
	}

	namespace ExitSettings
	{
		constexpr uint64_t Name = 0x18; // string
	}
	
	namespace SynchronizableObject
	{
		constexpr uint64_t Type = 0x68; // object
	}
	
	namespace SynchronizableObjectLogicProcessor
	{
		constexpr uint64_t _staticSynchronizableObjects = 0x18; // object
	}

	namespace TripwireSynchronizableObject
	{
		constexpr uint64_t _tripwireState = 0xE4; // object
		constexpr uint64_t ToPosition = 0x158; // object
	}

	namespace BtrController
	{
		constexpr uint64_t BtrView = 0x50; //_BtrView_k__BackingField
	}

	namespace BTRView
	{
		constexpr uint64_t turret = 0x60;
		constexpr uint64_t previousPosition = 0xB4;
	}

	namespace BTRTurretView
	{
		constexpr uint64_t attachedBot = 0x60;
	}

	namespace Player
	{
		constexpr uint64_t MovementContext = 0x60; // EFT.MovementContext
		constexpr uint64_t _playerBody = 0x190; // EFT.PlayerBody
		constexpr uint64_t Physical = 0x920; // -.\uE399 <Physical> Physical
		constexpr uint64_t Corpse = 0x680; // EFT.Interactive.Corpse
		constexpr uint64_t Location = 0x878; // String
		constexpr uint64_t Profile = 0x908; // EFT.Profile
		constexpr uint64_t ProceduralWeaponAnimation = 0x338; // EFT.Animations.ProceduralWeaponAnimation
		constexpr uint64_t _inventoryController = 0x980; // EFT.PlayerInventoryController update
		constexpr uint64_t _handsController = 0x988; // EFT.PlayerHands update
		constexpr uint64_t _playerLookRaycastTransform = 0xA18; // UnityEngine.Transform
	}

	namespace ObservedPlayerView
	{
		constexpr uint64_t ObservedPlayerController = 0x28; // EFT.NextObservedPlayer.ObservedPlayerController
		constexpr uint64_t Voice = 0x40; // string
		constexpr uint64_t Id = 0x7C; //int32_t
		constexpr uint64_t Side = 0x94; // EFT.EPlayerSide
		constexpr uint64_t AccountId = 0xC0; //_AccountId_k__BackingField
		constexpr uint64_t IsAI = 0xA0; // bool
		constexpr uint64_t PlayerBody = 0xD8; // EFT.PlayerBody
	}

	namespace ObservedPlayerController
	{
		constexpr uint64_t Player = 0x18; // EFT.NextObservedPlayer.ObservedPlayerView
		constexpr uint64_t MovementController = 0xD8; // -.\uED4F
		constexpr uint64_t HealthController = 0xE8; // -.\uE446
		constexpr uint64_t InventoryController = 0x10; // -.\uED5B update
		constexpr uint64_t HandsController = 0x120; // EFT.Animations.PlayerSpring
	}

	namespace ObservedMovementController
	{
		constexpr uint64_t ObservedPlayerStateContext = 0x98;
		constexpr uint64_t Rotation = 0x1c; // UnityEngine.Vector2 <HeadRotation> HeadRotation
		constexpr uint64_t Velocity = 0x30; // UnityEngine.Vector3 <Velocity> Velocity
	}

	namespace InventoryController
	{
		constexpr uint64_t Inventory = 0x100; // EFT.InventoryLogic.Inventory
	}

	namespace Inventory
	{
		constexpr uint64_t Equipment = 0x18; // EFT.InventoryLogic.InventoryEquipment
	}

	namespace InventoryEquipment
	{
		constexpr uint64_t _cachedSlots = 0x90; // EFT.InventoryLogic.Slot[]
	}

	namespace Slot
	{
		constexpr uint64_t ContainedItem = 0x48; // EFT.InventoryLogic.Item
		constexpr uint64_t ID = 0x58; // String
		constexpr uint64_t Required = 0x18; // Boolean
	}

	namespace BarterOtherOffsets
	{
		constexpr uint64_t Dogtag = 0x80; // EFT.InventoryLogic.BarterOther.Dogtag
	}

	namespace DogtagComponent
	{
		constexpr uint64_t Item = 0x10; // EFT.InventoryLogic.Item
		constexpr uint64_t GroupId = 0x18; // string
		constexpr uint64_t AccountId = 0x20; // string
		constexpr uint64_t ProfileId = 0x28; // string
		constexpr uint64_t Nickname = 0x30; // string
		constexpr uint64_t Side = 0x38; // EPlayerSide
		constexpr uint64_t Level = 0x3C; // int32_t
		constexpr uint64_t Time = 0x40; // DateTime
		constexpr uint64_t Status = 0x48; // string
		constexpr uint64_t KillerAccountId = 0x50; // string
		constexpr uint64_t KillerProfileId = 0x58; // string
		constexpr uint64_t KillerName = 0x60; // string
		constexpr uint64_t WeaponName = 0x68; // string
		constexpr uint64_t CarriedByGroupMember = 0x70; // bool
	}

	namespace ObservedPlayerStateContext
	{
		constexpr uint64_t Rotation = 0x28; // UnityEngine.Vector2
	}

	namespace ObservedMovementState
	{
		constexpr uint64_t ObservedPlayerHands = 0x140; // EFT.NextObservedPlayer.ObservedPlayerHandsController _observedPlayerHandsController 
	}

	namespace ObservedPlayerHands
	{
		constexpr uint64_t Item = 0x58; // EFT.InventoryLogic.Item _item 
	}

	namespace ObservedHealthController
	{
		constexpr uint64_t Player = 0x18; // EFT.NextObservedPlayer.ObservedPlayerView
		constexpr uint64_t PlayerCorpse = 0x20; // EFT.Interactive.ObservedCorpse
		constexpr uint64_t HealthStatus = 0x10; // System.Int32
	}

	namespace Profile
	{
		constexpr uint64_t Id = 0x10; // String
		constexpr uint64_t AccountId = 0x18; // String
		constexpr uint64_t Info = 0x48; // -.\uE9AD
		constexpr uint64_t QuestsData = 0x98; // object
		constexpr uint64_t WishlistManager = 0x108; // object
	}

	namespace WishlistManager
	{
		constexpr uint64_t _wishlistItems = 0x30; // object
	}

	namespace QuestsData
	{
		constexpr uint64_t Id = 0x10; // string
		constexpr uint64_t Status = 0x1C; // object
		constexpr uint64_t CompletedConditions = 0x28; // object
	}

	namespace PlayerInfo
	{
		constexpr uint64_t GroupId = 0x50; // String
		constexpr uint64_t Side = 0x48; // [HUMAN] Int32
		constexpr uint64_t RegistrationDate = 0x4C; // Int32
		constexpr uint64_t EntryPoint = 0x28; // String
	}

	namespace MovementContext
	{
		constexpr uint64_t Player = 0x48; // EFT.Player <_player> _player
		constexpr uint64_t PlantState = 0x78; // EFT.BaseMovementState <PlantState> PlantState
		constexpr uint64_t CurrentState = 0x1f0; // EFT.BaseMovementState <CurrentState>k__BackingField> <CurrentState>k__BackingField
		constexpr uint64_t _states = 0x480; // System.Collections.Generic.Dictionary<Byte, BaseMovementState> <_states> _states
		constexpr uint64_t _movementStates = 0x4b0; // -.IPlayerStateContainerBehaviour[] <_movementStates> _movementStates
		constexpr uint64_t _tilt = 0xb0; // Single <_tilt> _tilt
		constexpr uint64_t _rotation = 0xc8; // UnityEngine.Vector2 <_rotation> _rotation
		constexpr uint64_t _physicalCondition = 0x198; // System.Int32 <_physicalCondition> _physicalCondition
		constexpr uint64_t _speedLimitIsDirty = 0x1b9; // Boolean <_speedLimitIsDirty> _speedLimitIsDirty
		constexpr uint64_t StateSpeedLimit = 0x1bc; // Single <<StateSpeedLimit>k__BackingField> <StateSpeedLimit>k__BackingField
		constexpr uint64_t StateSprintSpeedLimit = 0x1c0; // Single <<StateSprintSpeedLimit>k__BackingField> <StateSprintSpeedLimit>k__BackingField
		constexpr uint64_t _lookDirection = 0x3b8; // UnityEngine.Vector3  <_lookDirection> _lookDirection
		constexpr uint64_t WalkInertia = 0x4bc; // Single <<WalkInertia>k__BackingField> <WalkInertia>k__BackingField
		constexpr uint64_t SprintBrakeInertia = 0x4c0; // Single <<SprintBrakeInertia>k__BackingField> <SprintBrakeInertia>k__BackingField
	}

	namespace ProceduralWeaponAnimation
	{
		constexpr uint64_t _isAiming = 0x14D; // Bool
		constexpr uint64_t _optics = 0x1A8; //class ptr
	}
	namespace SightNBone
	{
		constexpr uint64_t Mod = 0x10; //ptr
	}
	namespace SightComponent
	{
		inline constexpr std::uint64_t _template = 0x20; //
		inline constexpr std::uint64_t ScopeSelectedModes = 0x30; // system.int32[]
		inline constexpr std::uint64_t SelectedScope = 0x38; // int32
		inline constexpr std::uint64_t ScopeZoomValue = 0x3C; // single
	}
	namespace SightInterface
	{
		constexpr uint64_t Zooms = 0x1B8; //system.single[]
	}


	namespace InteractiveLootItem
	{
		constexpr uint64_t Item = 0xF0; // EFT.InventoryLogic.Item
	}

	namespace LootItemMod
	{
		constexpr uint64_t Grids = 0x78; // -.\uEE74[]
		constexpr uint64_t Slots = 0x80; // EFT.InventoryLogic.Slot[]
	}

	namespace LootableContainer
	{
		constexpr uint64_t ItemOwner = 0x168; //
	}

	namespace LootableContainerItemOwner
	{
		constexpr uint64_t RootItem = 0xD0; // EFT.InventoryLogic.Item
	}

	namespace LootItem
	{
		constexpr uint64_t Template = 0x60; // EFT.InventoryLogic.ItemTemplate
		constexpr uint64_t Version = 0x28; // Int32 update
	}

	namespace ItemTemplate
	{
		constexpr uint64_t ShortName = 0x18; // String
		constexpr uint64_t _id = 0xE0; // EFT.MongoID
		constexpr uint64_t QuestItem = 0x34; // Boolean
	}

	namespace Throwable
	{
		constexpr uint64_t _isDestroyed = 0x4D; // bool
	}

}