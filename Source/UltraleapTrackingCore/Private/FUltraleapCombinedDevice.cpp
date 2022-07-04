/******************************************************************************
 * Copyright (C) Ultraleap, Inc. 2011-2021.                                   *
 *                                                                            *
 * Use subject to the terms of the Apache License 2.0 available at            *
 * http://www.apache.org/licenses/LICENSE-2.0, or another agreement           *
 * between Ultraleap and you, your company or other organization.             *
 ******************************************************************************/

#include "FUltraleapCombinedDevice.h"

FUltraleapCombinedDevice::FUltraleapCombinedDevice(IHandTrackingWrapper* LeapDeviceWrapper,
	ITrackingDeviceWrapper* TrackingDeviceWrapperIn, TArray<IHandTrackingWrapper*> DevicesToCombineIn) : 
	FUltraleapDevice(LeapDeviceWrapper, TrackingDeviceWrapperIn),
	DevicesToCombine(DevicesToCombineIn)
{
}

FUltraleapCombinedDevice::~FUltraleapCombinedDevice()
{
	FUltraleapDevice::~FUltraleapDevice();
}
// Main loop event emitter and handler
void FUltraleapCombinedDevice::SendControllerEvents()
{
	// Create combined frame here and call parse
	// the parent class will then behave as if it had one device
	TArray<FLeapFrameData> SourceFrames;
	// add combiner logic based on DevicesToCombine List. All devices will have ticked before this is called
	for (auto SourceDevice : DevicesToCombine)
	{
		auto InternalSourceDevice = SourceDevice->GetDevice();
		if (InternalSourceDevice)
		{
			FLeapFrameData SourceFrame;
			InternalSourceDevice->GetLatestFrameData(SourceFrame);
			SourceFrames.Add(SourceFrame);
		}
	}
	
	CombineFrame(SourceFrames);
	ParseEvents();
}
FVector ToLocal(const FVector& WorldPoint,const FVector& LocalOrigin,const FQuat& LocalRot)
{
	return LocalRot.Inverse() * (WorldPoint - LocalOrigin);
}
FVector ToWorld(const FVector& LocalPoint,const FVector& LocalOrigin,const FQuat& LocalRot)
{
	return (LocalRot * LocalPoint) + LocalOrigin;
}
void FUltraleapCombinedDevice::CreateLocalLinearJointList(const FLeapHandData& Hand, TArray<FVector>& JointPositions)
{
	FVector PalmPos = Hand.Palm.Position;
	FQuat PalmRot = Hand.Palm.Orientation.Quaternion();

	int BoneIdx = 0;
	static const int NumFingers = 5;
	for (int i = 0; i < NumFingers; i++)
	{
		FVector BaseMetacarpal = ToLocal(Hand.Digits[i].Bones[0].PrevJoint, PalmPos, PalmRot);
		JointPositions[BoneIdx++] = BaseMetacarpal;
		
		static const int NumBones = 4;
		for (int j = 0; j < NumBones; j++)
		{
			FVector Joint = ToLocal(Hand.Digits[i].Bones[j].NextJoint, PalmPos, PalmRot);
			JointPositions[BoneIdx++] = Joint;
		}
	}
}


FLeapBoneData* GetBone(FLeapHandData& Hand, const int BoneIndex)
{
	return &Hand.Digits[BoneIndex / 4].Bones[BoneIndex % 4];
}
void FillBone(FLeapBoneData& Bone, const FVector& PrevJoint, const FVector& NextJoint,
	const float Width,const FQuat& Rotation)
{
	Bone.PrevJoint = PrevJoint;
	Bone.NextJoint = NextJoint;
	Bone.Rotation = Rotation.Rotator();
	Bone.Width = Width;
}
// Construct a hand from local space bones
void FUltraleapCombinedDevice::ConvertToWorldSpaceHand(
	FLeapHandData& Hand, const bool IsLeft, const FVector& PalmPos, const FQuat& PalmRot, const TArray<FVector>& JointPositions)
{
	// Create data structure
	EHandType HandType = EHandType::LEAP_HAND_RIGHT;

	if (IsLeft)
	{
		HandType = EHandType::LEAP_HAND_RIGHT;
	}
	Hand.InitFromEmpty(HandType);

	int BoneIdx = 0;
	FVector PrevJoint = FVector::ZeroVector;
	FVector NextJoint = FVector::ZeroVector;
	FQuat BoneRot = FQuat::Identity;

	// Fill fingers.
	static const int NumFingers = 5;
	for (int FingerIdx = 0; FingerIdx < NumFingers; FingerIdx++)
	{
		static const int NumJoints = 4;
		for (int JointIdx = 0; JointIdx < NumJoints; JointIdx++)
		{
			BoneIdx = FingerIdx * NumJoints + JointIdx;
			PrevJoint = JointPositions[FingerIdx * NumFingers + JointIdx];
			NextJoint = JointPositions[FingerIdx * NumFingers + JointIdx + 1];

			FVector Normalized(NextJoint - PrevJoint);
			Normalized.Normalize();
			
			if (Normalized == FVector::ZeroVector)
			{
				// Thumb "metacarpal" slot is an identity bone.
				BoneRot = FQuat::Identity;
			}
			else
			{
				// TODO: check up and right for LeapSpace v Unity and that FindBetweenNormals is correct as LookRotation replacement
				FVector Cross = FVector::CrossProduct(Normalized,(FingerIdx == 0 ? (IsLeft ? -FVector::UpVector : FVector::UpVector) : FVector::RightVector));
				BoneRot = FQuat::FindBetweenNormals(Normalized, Cross );
			}

			// Convert to world space from palm space.
			NextJoint = ToWorld(NextJoint, PalmPos, PalmRot);
			PrevJoint = ToWorld(PrevJoint, PalmPos, PalmRot);
			BoneRot = PalmRot * BoneRot;

			FillBone(*GetBone(Hand,BoneIdx), PrevJoint, NextJoint
										   , 1, 
										   BoneRot);
		}
		// Copy digits we just filled into top level data structures
		Hand.UpdateFromDigits();
	}

	// Fill arm data.
	FillBone(Hand.Arm, ToWorld(FVector(0.0f, 0.0f, -30.0f), PalmPos, PalmRot),
		ToWorld(FVector(0.0f, 0.0f, -5.5f), PalmPos, PalmRot),
		5,PalmRot);
	
	// Palm members
	Hand.Palm.Direction = PalmRot.GetForwardVector();
	// why zero in Unity?
	Hand.Palm.Normal = FVector::ZeroVector;
	Hand.Palm.Orientation = PalmRot.Rotator();
	Hand.Palm.Position = PalmPos;
	Hand.Palm.StabilizedPosition = PalmPos;
	Hand.Palm.Velocity = PalmPos;	// why palmpos in unity?
	Hand.Palm.Width = 8.5;
}
void FUltraleapCombinedDevice::CreateLinearJointListInterp(const FLeapHandData& Hand, TArray<FVector>& Joints)
{
	 // TODO used by Angular combiner
}