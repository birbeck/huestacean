#include "backend/backend.h"
#include "common/lightupdate.h"
#include "common/math.h"

#include <chrono>
#include <thread>
#include <algorithm>

using namespace std::chrono_literals;
using namespace Math;

Backend::Backend() :
	stopRequested(false),
	thread(),
	roomsMutex(),
	rooms(),
	activeRoomIndex(0),
	roomsAreDirty(false),
	deviceProviders()
{
}

Backend::~Backend()
{
	Stop();
}

void Backend::Start()
{
	//DOES NOT mutate rooms
	//DOES NOT read from rooms without locking roomsMutex

	if (IsRunning()) {
		return;
	}

	stopRequested = false;
	thread = std::thread([this] {
		Room renderRoom = rooms.size() > activeRoomIndex ? rooms[activeRoomIndex] : Room();

		LightUpdateParams lightUpdate;
		std::vector<HsluvColor> colors;
		std::vector<Box> boundingBoxes;
		std::vector<DevicePtr> devices;

		auto tick = [&](float deltaTime)
		{
			//Copy the new room if necessary
			// @TODO multiple active rooms
			if (roomsAreDirty)
			{
				{
					std::scoped_lock lock(roomsMutex);
					int updateThreadRoomIndex = activeRoomIndex;
					renderRoom = rooms.size() > updateThreadRoomIndex ? rooms[updateThreadRoomIndex] : Room();
				}

				//Update and dirty Positions and Devices
				lightUpdate.colorsDirty = true;
				lightUpdate.positionsDirty = true;
				lightUpdate.devicesDirty = true;

				//Sort devices by ProviderType     
				std::sort(renderRoom.devices.begin(), renderRoom.devices.end(),
					[&](const DeviceInRoom & a, const DeviceInRoom & b) {
						if (a.device->GetType() == b.device->GetType())
						{
							return deviceProviders[a.device->GetType()]->compare(a, b);
						}
						else
						{
							return compare(a.device, b.device);
						}
					});

				//Query every device to fetch positions off it 
				//	+ Fill in big dumb non-sparse Devices array
				boundingBoxes.clear();
				devices.clear();

				for (const auto& d : renderRoom.devices)
				{
					auto boxesToAppend = d.GetLightBoundingBoxes();
					auto devicesToAppend = std::vector<DevicePtr>(boxesToAppend.size(), d.device);

					boundingBoxes.insert(boundingBoxes.end(), boxesToAppend.begin(), boxesToAppend.end());
					devices.insert(devices.end(), devicesToAppend.begin(), devicesToAppend.end());
				}


				//Blank out Colors
				colors.clear();
				colors = { boundingBoxes.size() };

				//@TODO
			}

			//Run effects
			for (auto& effect : renderRoom.effects)
			{
				effect->Tick(deltaTime);
				effect->Update(boundingBoxes, colors);
			}
			lightUpdate.colorsDirty = true;

			//Send light data to device providers

			//@TODO

			//DONE
		};

		auto lastStart = std::chrono::high_resolution_clock::now();

		while (!stopRequested) {
			constexpr auto tickRate = 16.67ms;

			auto start = std::chrono::high_resolution_clock::now();
			float deltaTime = std::chrono::duration<float>{ start - lastStart }.count();
			lastStart = start;

			tick(deltaTime);

			auto end = std::chrono::high_resolution_clock::now();

			//sleep to keep our tick rate right, or at least 1ms
			auto timeLeft = tickRate - (end - start);
			auto sleepForTime = timeLeft > 1ms ? timeLeft : 1ms;
			std::this_thread::sleep_for(sleepForTime);
		}
	});
}

bool Backend::IsRunning()
{
	return thread.joinable();
}

void Backend::Stop()
{
	if (!IsRunning()) {
		return;
	}

	stopRequested = true;
	thread.join();
}

const std::vector<Room>& Backend::GetRooms() const
{
	return rooms;
}

Backend::RoomsWriter Backend::GetRoomsWriter()
{
	return RoomsWriter(this);
}