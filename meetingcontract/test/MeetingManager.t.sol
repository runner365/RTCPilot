// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "forge-std/Test.sol";
import "../src/MeetingManager.sol";
import "../src/MeetingToken.sol";

contract MeetingManagerTest is Test {
    MeetingManager public manager;
    MeetingToken public token;

    address public owner = address(0x1000);
    address public creator = address(0x2000);
    address public attendee = address(0x3000);
    address public attendee2 = address(0x4000);

    uint256 public constant INITIAL_SUPPLY = 1_000_000 * 10 ** 18;
    uint256 public constant CREATION_FEE = 10 * 10 ** 18;
    uint256 public constant FEE_PER_ATTENDEE = 50 * 10 ** 18;

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

    function setUp() public {
        vm.startPrank(owner);
        token = new MeetingToken(INITIAL_SUPPLY, owner);
        manager = new MeetingManager(address(token), CREATION_FEE);

        // Fund creator and attendees with tokens
        token.transfer(creator, 1000 * 10 ** 18);
        token.transfer(attendee, 1000 * 10 ** 18);
        token.transfer(attendee2, 1000 * 10 ** 18);
        vm.stopPrank();
    }

    function test_Constructor() public view {
        assertEq(address(manager.meetingToken()), address(token));
        assertEq(manager.creationFee(), CREATION_FEE);
        assertEq(manager.owner(), owner);
        assertEq(manager.getMeetingCount(), 0);
    }

    function test_Revert_Constructor_ZeroToken() public {
        vm.expectRevert("Invalid meeting token");
        new MeetingManager(address(0), CREATION_FEE);
    }

    function test_Revert_Constructor_ZeroCreationFee() public {
        vm.expectRevert("Creation fee must be greater than zero");
        new MeetingManager(address(token), 0);
    }

    function test_SetCreationFee() public {
        uint256 newFee = 20 * 10 ** 18;

        vm.prank(owner);
        vm.expectEmit(true, true, true, true);
        emit CreationFeeUpdated(CREATION_FEE, newFee);
        manager.setCreationFee(newFee);

        assertEq(manager.creationFee(), newFee);
    }

    function test_Revert_SetCreationFee_NotOwner() public {
        vm.prank(creator);
        vm.expectRevert();
        manager.setCreationFee(100);
    }

    function test_CreateMeeting() public {
        uint256 futureStart = block.timestamp + 1 hours;
        uint256 futureEnd = block.timestamp + 2 hours;

        vm.prank(creator);
        token.approve(address(manager), CREATION_FEE);

        vm.prank(creator);
        vm.expectEmit(true, true, true, true);
        emit MeetingCreated(1, creator, "Test Meeting", FEE_PER_ATTENDEE, futureStart, futureEnd);
        uint256 meetingId = manager.createMeeting(
            "Test Meeting",
            "A test meeting description",
            futureStart,
            futureEnd,
            FEE_PER_ATTENDEE
        );

        assertEq(meetingId, 1);
        assertEq(manager.getMeetingCount(), 1);
        assertEq(token.balanceOf(address(manager)), CREATION_FEE);

        MeetingManager.Meeting memory m = manager.getMeeting(meetingId);
        assertEq(m.creator, creator);
        assertEq(m.name, "Test Meeting");
        assertEq(m.description, "A test meeting description");
        assertEq(m.startTime, futureStart);
        assertEq(m.endTime, futureEnd);
        assertEq(m.feePerAttendee, FEE_PER_ATTENDEE);
        assertEq(m.attendeeCount, 0);
        assertTrue(m.active);
    }

    function test_Revert_CreateMeeting_EmptyName() public {
        uint256 futureStart = block.timestamp + 1 hours;
        uint256 futureEnd = block.timestamp + 2 hours;

        vm.prank(creator);
        vm.expectRevert("Name must not be empty");
        manager.createMeeting("", "desc", futureStart, futureEnd, FEE_PER_ATTENDEE);
    }

    function test_Revert_CreateMeeting_InvalidTime() public {
        uint256 futureStart = block.timestamp + 1 hours;
        uint256 futureEnd = block.timestamp;

        vm.prank(creator);
        vm.expectRevert("End time must be after start time");
        manager.createMeeting("Test", "desc", futureStart, futureEnd, FEE_PER_ATTENDEE);
    }

    function test_Revert_CreateMeeting_ZeroFee() public {
        uint256 futureStart = block.timestamp + 1 hours;
        uint256 futureEnd = block.timestamp + 2 hours;

        vm.prank(creator);
        vm.expectRevert("Fee per attendee must be greater than zero");
        manager.createMeeting("Test", "desc", futureStart, futureEnd, 0);
    }

    function test_JoinMeeting() public {
        uint256 startTime = block.timestamp; // starts now
        uint256 endTime = block.timestamp + 1 hours;

        // Create meeting
        vm.prank(creator);
        token.approve(address(manager), CREATION_FEE);
        vm.prank(creator);
        uint256 meetingId = manager.createMeeting("Test", "desc", startTime, endTime, FEE_PER_ATTENDEE);

        // Join meeting
        vm.prank(attendee);
        token.approve(address(manager), FEE_PER_ATTENDEE);

        uint256 creatorBalanceBefore = token.balanceOf(creator);

        vm.prank(attendee);
        vm.expectEmit(true, true, true, true);
        emit MeetingJoined(meetingId, attendee, FEE_PER_ATTENDEE);
        manager.joinMeeting(meetingId);

        assertEq(token.balanceOf(creator), creatorBalanceBefore + FEE_PER_ATTENDEE);
        assertTrue(manager.hasJoined(meetingId, attendee));

        MeetingManager.Meeting memory m = manager.getMeeting(meetingId);
        assertEq(m.attendeeCount, 1);
    }

    function test_JoinMeeting_MultipleAttendees() public {
        uint256 startTime = block.timestamp;
        uint256 endTime = block.timestamp + 1 hours;

        vm.prank(creator);
        token.approve(address(manager), CREATION_FEE);
        vm.prank(creator);
        uint256 meetingId = manager.createMeeting("Test", "desc", startTime, endTime, FEE_PER_ATTENDEE);

        uint256 creatorBalanceBefore = token.balanceOf(creator);

        vm.prank(attendee);
        token.approve(address(manager), FEE_PER_ATTENDEE);
        vm.prank(attendee);
        manager.joinMeeting(meetingId);

        vm.prank(attendee2);
        token.approve(address(manager), FEE_PER_ATTENDEE);
        vm.prank(attendee2);
        manager.joinMeeting(meetingId);

        assertEq(token.balanceOf(creator), creatorBalanceBefore + FEE_PER_ATTENDEE * 2);
        assertEq(manager.getMeeting(meetingId).attendeeCount, 2);
    }

    function test_Revert_JoinMeeting_NotActive() public {
        uint256 futureStart = block.timestamp + 1 hours;
        uint256 futureEnd = block.timestamp + 2 hours;

        vm.prank(creator);
        token.approve(address(manager), CREATION_FEE);
        vm.prank(creator);
        uint256 meetingId = manager.createMeeting("Test", "desc", futureStart, futureEnd, FEE_PER_ATTENDEE);

        vm.prank(creator);
        manager.cancelMeeting(meetingId);

        vm.prank(attendee);
        vm.expectRevert("Meeting is not active");
        manager.joinMeeting(meetingId);
    }

    function test_Revert_JoinMeeting_CreatorJoin() public {
        uint256 startTime = block.timestamp;
        uint256 endTime = block.timestamp + 1 hours;

        vm.prank(creator);
        token.approve(address(manager), CREATION_FEE);
        vm.prank(creator);
        uint256 meetingId = manager.createMeeting("Test", "desc", startTime, endTime, FEE_PER_ATTENDEE);

        vm.prank(creator);
        vm.expectRevert("Creator cannot join own meeting");
        manager.joinMeeting(meetingId);
    }

    function test_Revert_JoinMeeting_NotStarted() public {
        uint256 futureStart = block.timestamp + 1 hours;
        uint256 futureEnd = block.timestamp + 2 hours;

        vm.prank(creator);
        token.approve(address(manager), CREATION_FEE);
        vm.prank(creator);
        uint256 meetingId = manager.createMeeting("Test", "desc", futureStart, futureEnd, FEE_PER_ATTENDEE);

        vm.prank(attendee);
        vm.expectRevert("Meeting has not started");
        manager.joinMeeting(meetingId);
    }

    function test_Revert_JoinMeeting_Twice() public {
        uint256 startTime = block.timestamp;
        uint256 endTime = block.timestamp + 1 hours;

        vm.prank(creator);
        token.approve(address(manager), CREATION_FEE);
        vm.prank(creator);
        uint256 meetingId = manager.createMeeting("Test", "desc", startTime, endTime, FEE_PER_ATTENDEE);

        vm.prank(attendee);
        token.approve(address(manager), FEE_PER_ATTENDEE * 2);
        vm.prank(attendee);
        manager.joinMeeting(meetingId);

        vm.prank(attendee);
        vm.expectRevert("Already joined this meeting");
        manager.joinMeeting(meetingId);
    }

    function test_CancelMeeting() public {
        uint256 futureStart = block.timestamp + 1 hours;
        uint256 futureEnd = block.timestamp + 2 hours;

        vm.prank(creator);
        token.approve(address(manager), CREATION_FEE);
        vm.prank(creator);
        uint256 meetingId = manager.createMeeting("Test", "desc", futureStart, futureEnd, FEE_PER_ATTENDEE);

        uint256 managerBalanceBefore = token.balanceOf(address(manager));

        vm.prank(creator);
        vm.expectEmit(true, true, true, true);
        emit MeetingCancelled(meetingId);
        manager.cancelMeeting(meetingId);

        assertEq(token.balanceOf(address(manager)), managerBalanceBefore - CREATION_FEE);
        assertFalse(manager.getMeeting(meetingId).active);
    }

    function test_Revert_CancelMeeting_NotCreator() public {
        uint256 futureStart = block.timestamp + 1 hours;
        uint256 futureEnd = block.timestamp + 2 hours;

        vm.prank(creator);
        token.approve(address(manager), CREATION_FEE);
        vm.prank(creator);
        uint256 meetingId = manager.createMeeting("Test", "desc", futureStart, futureEnd, FEE_PER_ATTENDEE);

        vm.prank(attendee);
        vm.expectRevert("Only creator can cancel");
        manager.cancelMeeting(meetingId);
    }

    function test_Revert_CancelMeeting_AlreadyStarted() public {
        uint256 startTime = block.timestamp;
        uint256 endTime = block.timestamp + 1 hours;

        vm.prank(creator);
        token.approve(address(manager), CREATION_FEE);
        vm.prank(creator);
        uint256 meetingId = manager.createMeeting("Test", "desc", startTime, endTime, FEE_PER_ATTENDEE);

        vm.prank(creator);
        vm.expectRevert("Meeting has already started");
        manager.cancelMeeting(meetingId);
    }

    function test_GetMeeting_Revert_InvalidId() public {
        vm.expectRevert("Meeting does not exist");
        manager.getMeeting(0);

        vm.expectRevert("Meeting does not exist");
        manager.getMeeting(999);
    }
}
