using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Networking;

public class PlayerController : NetworkBehaviour {

    public GameObject bulletPrefab;
    public Transform bulletSpawn;

    void Update() {
        if (!isLocalPlayer)
            return;

        if (Input.GetKeyDown(KeyCode.Space)) {
            CmdFire();
        }

        var x = Input.GetAxis("Horizontal") * Time.deltaTime * 150.0f;
        var z = Input.GetAxis("Vertical") * Time.deltaTime * 3.0f;

        transform.Rotate(0, x, 0);
        transform.Translate(0, 0, z);

    }

    [Command]
    void CmdFire() {
        GameObject newBullet = Instantiate(bulletPrefab, bulletSpawn.position, bulletSpawn.rotation) as GameObject;
        newBullet.GetComponent<Rigidbody>().velocity = newBullet.transform.forward * 6.0f;

        NetworkServer.Spawn(newBullet);

        Destroy(newBullet, 2.0f);   
    }



    public override void OnStartLocalPlayer() {
        GetComponent<MeshRenderer>().material.color = Color.cyan;
    }
}