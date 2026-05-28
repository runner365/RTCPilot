// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "@openzeppelin/access/Ownable.sol";
import "@openzeppelin/token/ERC20/IERC20.sol";
import "@openzeppelin/utils/ReentrancyGuard.sol";

contract MeetingManager is Ownable, ReentrancyGuard {
    IERC20 public immutable meetingToken;

    uint256 public creationFee;
    uint256 private _meetingCounter;

    struct Meeting {
        address creator;
        string name;
        string description;
        uint256 startTime;
        uint256 endTime;
        uint256 feePerAttendee;
        uint256 attendeeCount;
        bool active;
    }

    mapping(uint256 => Meeting) private _meetings;
    // meetingId => attendee => hasPaid
    mapping(uint256 => mapping(address => bool)) private _attendees;

    event MeetingCreated(
        uint256 indexed meetingId,
        address indexed creator,
        string name,
        uint256 feePerAttendee,
        uint256 startTime,
        uint256 endTime
    );
    event MeetingJoined(
        uint256 indexed meetingId,
        address indexed attendee,
        uint256 feePaid
    );
    event MeetingCancelled(uint256 indexed meetingId);
    event CreationFeeUpdated(uint256 oldFee, uint256 newFee);

    constructor(
        address meetingToken_,
        uint256 creationFee_
    ) Ownable(msg.sender) {
        require(meetingToken_ != address(0), "Invalid meeting token");
        require(creationFee_ > 0, "Creation fee must be greater than zero");
        meetingToken = IERC20(meetingToken_);
        creationFee = creationFee_;
        _meetingCounter = 0;
    }

    function setCreationFee(uint256 newFee) external onlyOwner {
        require(newFee > 0, "Creation fee must be greater than zero");
        uint256 oldFee = creationFee;
        creationFee = newFee;
        emit CreationFeeUpdated(oldFee, newFee);
    }

    function createMeeting(
        string calldata name,
        string calldata description,
        uint256 startTime,
        uint256 endTime,
        uint256 feePerAttendee
    ) external nonReentrant returns (uint256 meetingId) {
        require(bytes(name).length > 0, "Name must not be empty");
        require(endTime > startTime, "End time must be after start time");
        require(feePerAttendee > 0, "Fee per attendee must be greater than zero");

        meetingToken.transferFrom(msg.sender, address(this), creationFee);

        meetingId = ++_meetingCounter;
        _meetings[meetingId] = Meeting({
            creator: msg.sender,
            name: name,
            description: description,
            startTime: startTime,
            endTime: endTime,
            feePerAttendee: feePerAttendee,
            attendeeCount: 0,
            active: true
        });

        emit MeetingCreated(meetingId, msg.sender, name, feePerAttendee, startTime, endTime);
    }

    function joinMeeting(uint256 meetingId) external nonReentrant {
        Meeting storage m = _meetings[meetingId];
        require(m.active, "Meeting is not active");
        require(msg.sender != m.creator, "Creator cannot join own meeting");
        require(block.timestamp >= m.startTime, "Meeting has not started");
        require(block.timestamp <= m.endTime, "Meeting has ended");
        require(!_attendees[meetingId][msg.sender], "Already joined this meeting");

        _attendees[meetingId][msg.sender] = true;
        m.attendeeCount++;

        meetingToken.transferFrom(msg.sender, m.creator, m.feePerAttendee);

        emit MeetingJoined(meetingId, msg.sender, m.feePerAttendee);
    }

    function cancelMeeting(uint256 meetingId) external {
        Meeting storage m = _meetings[meetingId];
        require(msg.sender == m.creator, "Only creator can cancel");
        require(m.active, "Meeting is not active");
        require(block.timestamp < m.startTime, "Meeting has already started");

        m.active = false;
        meetingToken.transfer(msg.sender, creationFee);

        emit MeetingCancelled(meetingId);
    }

    function getMeeting(uint256 meetingId) external view returns (Meeting memory) {
        require(meetingId > 0 && meetingId <= _meetingCounter, "Meeting does not exist");
        return _meetings[meetingId];
    }

    function getMeetingCount() external view returns (uint256) {
        return _meetingCounter;
    }

    function hasJoined(uint256 meetingId, address attendee) external view returns (bool) {
        return _attendees[meetingId][attendee];
    }
}
